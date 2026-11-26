/*
 * frame_server.c — minimal HTTP + WebSocket server carrying the simulator's
 * window as JPEG frames, with JSON input back from the browser.
 *
 * Deliberately dependency-light: a hand-rolled WebSocket (RFC 6455, no
 * extensions/fragmentation) plus libjpeg. ~600 lines instead of pulling in
 * libwebsockets, which is all this dev tool needs.
 *
 * Threading: one thread does select() + encode + send. It never touches LVGL;
 * frames come from hal_display_snapshot() (locked inside the display backend)
 * and input goes out as SDL events / HAL key events.
 */

#include "frame_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <jpeglib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "bezel.h"
#include "hal/hal_input.h"
#include "hal_sdl_host.h"

#define DEFAULT_PORT   8099
#define PORT_TRIES     12
#define MAX_CLIENTS    4
#define RX_CAP         8192
#define TAP_HOLD_MS    160
#define MAX_PENDING    8

/* ------------------------------------------------------------------ */
/* Minimal SHA-1 + base64 (WebSocket handshake)                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t  buf[64];
    size_t   buflen;
} sha1_ctx;

static uint32_t rotl32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static void sha1_block(sha1_ctx *c, const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & cc) | ((~b) & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ cc ^ d;                      k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d);   k = 0x8F1BBCDC; }
        else             { f = b ^ cc ^ d;                      k = 0xCA62C1D6; }
        uint32_t tmp = rotl32(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = rotl32(b, 30); b = a; a = tmp;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void sha1_init(sha1_ctx *c)
{
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
    c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
    c->len = 0;
    c->buflen = 0;
}

static void sha1_update(sha1_ctx *c, const uint8_t *p, size_t n)
{
    c->len += n;
    while (n) {
        size_t take = 64 - c->buflen;
        if (take > n) take = n;
        memcpy(c->buf + c->buflen, p, take);
        c->buflen += take;
        p += take;
        n -= take;
        if (c->buflen == 64) {
            sha1_block(c, c->buf);
            c->buflen = 0;
        }
    }
}

static void sha1_final(sha1_ctx *c, uint8_t out[20])
{
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80;
    sha1_update(c, &pad, 1);
    uint8_t zero = 0;
    while (c->buflen != 56) {
        sha1_update(c, &zero, 1);
    }
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; i++) {
        lenbuf[i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    sha1_update(c, lenbuf, 8);
    for (int i = 0; i < 5; i++) {
        out[i * 4]     = (uint8_t)(c->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->h[i]);
    }
}

static void base64_encode(const uint8_t *in, size_t n, char *out)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = tbl[(v >> 18) & 0x3F];
        out[o++] = tbl[(v >> 12) & 0x3F];
        out[o++] = (i + 1 < n) ? tbl[(v >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < n) ? tbl[v & 0x3F] : '=';
    }
    out[o] = '\0';
}

/* ------------------------------------------------------------------ */
/* The page                                                           */
/* ------------------------------------------------------------------ */

static const char PAGE_FMT[] =
"<!doctype html><html><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,"
"maximum-scale=1,user-scalable=no,viewport-fit=cover\">"
"<title>dumbphone</title><style>"
"html{height:100%%}"
"body{margin:0;height:100%%;height:100dvh;background:#0b0f0c;overflow:hidden;"
"display:flex;align-items:center;justify-content:center;touch-action:none;"
"-webkit-user-select:none;user-select:none;-webkit-tap-highlight-color:transparent}"
"canvas{display:block;image-rendering:pixelated;image-rendering:crisp-edges;"
"touch-action:none}"
"</style></head><body>"
"<canvas id=\"c\" width=\"%d\" height=\"%d\"></canvas><script>"
"const cv=document.getElementById('c'),ctx=cv.getContext('2d',{alpha:false});"
"let ws=null;"
/* Integer-scale fit, in DEVICE pixels. CSS pixels are not pixels: a tablet
 * with devicePixelRatio 2.5 maps one CSS px onto 2.5 real ones, so a
 * whole-number CSS scale still lands each source pixel on 2.5 screen pixels —
 * nearest-neighbour then alternates between 2 and 3 device pixels and the
 * result reads as aliasing. Pick the largest integer scale in DEVICE pixels
 * and convert back to CSS for layout. */
"function fit(){const dpr=window.devicePixelRatio||1;"
"const D=Math.max(1,Math.floor(Math.min(innerWidth*dpr/cv.width,innerHeight*dpr/cv.height)));"
"cv.style.width=(cv.width*D/dpr)+'px';cv.style.height=(cv.height*D/dpr)+'px';}"
"addEventListener('resize',fit);addEventListener('orientationchange',fit);fit();"
"function draw(b){if(createImageBitmap){createImageBitmap(b).then(m=>{ctx.drawImage(m,0,0);m.close();})}"
"else{const u=URL.createObjectURL(b),i=new Image();i.onload=()=>{ctx.drawImage(i,0,0);URL.revokeObjectURL(u);};i.src=u;}}"
"function connect(){ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');"
"ws.binaryType='blob';"
"ws.onmessage=e=>{if(e.data instanceof Blob)draw(e.data);};"
"ws.onclose=()=>setTimeout(connect,800);ws.onerror=()=>ws.close();}"
"function send(o){if(ws&&ws.readyState===1)ws.send(JSON.stringify(o));}"
"function pt(e){const r=cv.getBoundingClientRect();"
"let x=Math.round((e.clientX-r.left)/r.width*cv.width);"
"let y=Math.round((e.clientY-r.top)/r.height*cv.height);"
"return {x:Math.max(0,Math.min(cv.width-1,x)),y:Math.max(0,Math.min(cv.height-1,y))};}"
"cv.addEventListener('pointerdown',e=>{e.preventDefault();const p=pt(e);send({tap:true,x:p.x,y:p.y});});"
"cv.addEventListener('contextmenu',e=>e.preventDefault());"
"connect();</script></body></html>";

/* ------------------------------------------------------------------ */
/* Server state                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    int      fd;
    bool     ws;      /* upgraded and ready for frames */
    bool     dead;    /* close at the end of this loop pass */
    uint8_t  rx[RX_CAP];
    size_t   rxlen;
} client_t;

typedef struct {
    bool      used;
    bool      is_key;      /* false = tap (x,y), true = named key */
    int       x, y;
    hal_key_t key;
    uint64_t  due_ms;
} pending_release_t;

static pthread_t        s_thread;
static volatile bool    s_running;
static int              s_listen = -1;
static int              s_port;
static client_t         s_clients[MAX_CLIENTS];
static uint32_t        *s_argb;          /* staging copy of the window  */
static uint32_t        *s_prev;          /* last frame actually sent    */
static bool             s_have_prev;
static volatile bool    s_force_frame;   /* a client just joined        */
static int              s_win_w, s_win_h;
static int              s_fps = 18;
static pending_release_t s_pending[MAX_PENDING];
static pthread_mutex_t  s_pending_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/* ---- input ------------------------------------------------------- */

static void queue_release(bool is_key, int x, int y, hal_key_t key)
{
    pthread_mutex_lock(&s_pending_lock);
    for (int i = 0; i < MAX_PENDING; i++) {
        if (!s_pending[i].used) {
            s_pending[i].used = true;
            s_pending[i].is_key = is_key;
            s_pending[i].x = x;
            s_pending[i].y = y;
            s_pending[i].key = key;
            s_pending[i].due_ms = now_ms() + TAP_HOLD_MS;
            break;
        }
    }
    pthread_mutex_unlock(&s_pending_lock);
}

static void push_mouse(SDL_EventType type, int x, int y)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.x = x;
    ev.button.y = y;
    SDL_PushEvent(&ev);
}

/* A tap: same SDL path as a real click (bezel hit test or touch). The release
 * is delayed so the key visibly depresses in the next frame. */
static void do_tap(int x, int y)
{
    push_mouse(SDL_MOUSEBUTTONDOWN, x, y);
    queue_release(false, x, y, 0);
}

static void do_key(const char *name)
{
    /* A bezel cap? Press it by synthesising a click at its centre, which also
     * lights the key up for the user. */
    for (size_t i = 0; i < bezel_button_count(); i++) {
        const bezel_button_t *b = bezel_button(i);
        if (strcmp(b->id, name) != 0) {
            continue;
        }
        int x = (int)(((float)b->x + b->w / 2.0f) * bezel_scale());
        int y = (int)(((float)b->y + b->h / 2.0f) * bezel_scale());
        push_mouse(SDL_MOUSEBUTTONDOWN, x, y);
        queue_release(false, x, y, 0);
        return;
    }
    /* Otherwise a keyboard-only key. */
    hal_key_t key;
    if      (!strcmp(name, "home")) key = HAL_KEY_HOME;
    else if (!strcmp(name, "back")) key = HAL_KEY_BACK;
    else if (!strcmp(name, "menu")) key = HAL_KEY_MENU;
    else return;
    hal_input_inject_key(key, true);
    queue_release(true, 0, 0, key);
}

static void flush_releases(void)
{
    uint64_t t = now_ms();
    pthread_mutex_lock(&s_pending_lock);
    for (int i = 0; i < MAX_PENDING; i++) {
        pending_release_t *p = &s_pending[i];
        if (!p->used || p->due_ms > t) {
            continue;
        }
        if (p->is_key) {
            hal_input_inject_key(p->key, false);
        } else {
            push_mouse(SDL_MOUSEBUTTONUP, p->x, p->y);
        }
        p->used = false;
    }
    pthread_mutex_unlock(&s_pending_lock);
}

/* ---- tiny JSON scan ---------------------------------------------- */

static bool json_int(const char *s, const char *key, int *out)
{
    char pat[24];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat);
    if (!p) {
        return false;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p < '0' || *p > '9') {
        if (*p != '-') return false;
    }
    *out = (int)strtol(p, NULL, 10);
    return true;
}

static bool json_str(const char *s, const char *key, char *out, size_t out_len)
{
    char pat[24];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat);
    if (!p) {
        return false;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') {
        return false;
    }
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_len) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

static void handle_message(const char *json, size_t len)
{
    char buf[512];
    if (len >= sizeof buf) {
        return;
    }
    memcpy(buf, json, len);
    buf[len] = '\0';

    int x, y;
    if (json_int(buf, "x", &x) && json_int(buf, "y", &y) &&
        strstr(buf, "\"tap\"")) {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= s_win_w) x = s_win_w - 1;
        if (y >= s_win_h) y = s_win_h - 1;
        do_tap(x, y);
        return;
    }
    char key[32];
    if (json_str(buf, "key", key, sizeof key)) {
        do_key(key);
    }
}

/* ---- WebSocket framing ------------------------------------------- */

static int ws_send_frame(int fd, const void *data, size_t len, uint8_t opcode)
{
    uint8_t hdr[10];
    size_t hl;
    hdr[0] = (uint8_t)(0x80 | opcode);
    if (len < 126) {
        hdr[1] = (uint8_t)len;
        hl = 2;
    } else if (len <= 0xFFFF) {
        hdr[1] = 126;
        hdr[2] = (uint8_t)(len >> 8);
        hdr[3] = (uint8_t)(len & 0xFF);
        hl = 4;
    } else {
        hdr[1] = 127;
        for (int i = 0; i < 8; i++) {
            hdr[2 + i] = (uint8_t)(len >> (56 - 8 * i));
        }
        hl = 10;
    }
    if (send_all(fd, hdr, hl) != 0) {
        return -1;
    }
    return len ? send_all(fd, data, len) : 0;
}

static void client_parse(client_t *c)
{
    size_t off = 0;
    while (c->rxlen - off >= 2) {
        const uint8_t *f = c->rx + off;
        int opcode = f[0] & 0x0F;
        bool masked = (f[1] & 0x80) != 0;
        uint64_t plen = f[1] & 0x7F;
        size_t hdr = 2;
        if (plen == 126) {
            if (c->rxlen - off < 4) break;
            plen = ((uint64_t)f[2] << 8) | f[3];
            hdr = 4;
        } else if (plen == 127) {
            if (c->rxlen - off < 10) break;
            plen = 0;
            for (int i = 0; i < 8; i++) plen = (plen << 8) | f[2 + i];
            hdr = 10;
        }
        size_t maskoff = hdr;
        if (masked) hdr += 4;
        if (c->rxlen - off < hdr + plen) break;      /* incomplete */
        if (plen > 1024) { c->dead = true; return; }  /* JSON only, tiny */

        char msg[1025];
        const uint8_t *payload = f + hdr;
        for (uint64_t i = 0; i < plen; i++) {
            uint8_t b = payload[i];
            if (masked) b ^= f[maskoff + (i & 3)];
            msg[i] = (char)b;
        }
        msg[plen] = '\0';

        if (opcode == 0x1) {
            handle_message(msg, (size_t)plen);
        } else if (opcode == 0x8) {
            c->dead = true;
        } else if (opcode == 0x9) {
            ws_send_frame(c->fd, msg, (size_t)plen, 0xA);   /* pong */
        }
        off += hdr + plen;
    }
    if (off) {
        memmove(c->rx, c->rx + off, c->rxlen - off);
        c->rxlen -= off;
    }
}

/* ---- HTTP -------------------------------------------------------- */

static void serve_page(int fd)
{
    char body[8192];
    int n = snprintf(body, sizeof body, PAGE_FMT, s_win_w, s_win_h);
    char hdr[256];
    int h = snprintf(hdr, sizeof hdr,
                     "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                     "Content-Length: %d\r\nCache-Control: no-store\r\n"
                     "Connection: close\r\n\r\n", n);
    send_all(fd, hdr, (size_t)h);
    send_all(fd, body, (size_t)n);
}

static bool do_handshake(int fd, const char *req)
{
    const char *k = strstr(req, "Sec-WebSocket-Key:");
    if (!k) {
        return false;
    }
    k += strlen("Sec-WebSocket-Key:");
    while (*k == ' ') k++;
    char key[128];
    size_t i = 0;
    while (k[i] && k[i] != '\r' && k[i] != '\n' && i + 1 < sizeof key) {
        key[i] = k[i];
        i++;
    }
    key[i] = '\0';

    char magic[256];   /* key (<=127) + the 36-byte GUID */
    snprintf(magic, sizeof magic, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    sha1_ctx ctx;
    uint8_t digest[20];
    sha1_init(&ctx);
    sha1_update(&ctx, (const uint8_t *)magic, strlen(magic));
    sha1_final(&ctx, digest);
    char accept[32];
    base64_encode(digest, sizeof digest, accept);

    char resp[320];
    int n = snprintf(resp, sizeof resp,
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    return send_all(fd, resp, (size_t)n) == 0;
}

static void handle_client_read(client_t *c)
{
    if (c->rxlen >= RX_CAP) {
        c->dead = true;
        return;
    }
    ssize_t n = recv(c->fd, c->rx + c->rxlen, RX_CAP - c->rxlen, 0);
    if (n <= 0) {
        c->dead = true;
        return;
    }
    c->rxlen += (size_t)n;

    if (c->ws) {
        client_parse(c);
        return;
    }

    /* Plain HTTP: wait for the full header block. */
    c->rx[c->rxlen] = '\0';
    if (!strstr((char *)c->rx, "\r\n\r\n")) {
        return;
    }
    if (strstr((char *)c->rx, "Upgrade: websocket") ||
        strstr((char *)c->rx, "upgrade: websocket")) {
        if (do_handshake(c->fd, (char *)c->rx)) {
            c->ws = true;
            c->rxlen = 0;
            s_force_frame = true;   /* send the current screen right away */
            return;
        }
    }
    serve_page(c->fd);
    c->dead = true;
}

/* ---- frames ------------------------------------------------------ */

static bool encode_jpeg(const uint32_t *argb, int w, int h,
                        unsigned char **out, unsigned long *out_len)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    *out = NULL;
    *out_len = 0;
    jpeg_mem_dest(&cinfo, out, out_len);
    cinfo.image_width = (JDIMENSION)w;
    cinfo.image_height = (JDIMENSION)h;
    cinfo.input_components = 4;
    cinfo.in_color_space = JCS_EXT_BGRX;      /* ARGB8888 in memory */
    jpeg_set_defaults(&cinfo);
    /* Flat pixel art: keep every channel at full resolution (4:4:4). The
     * default 4:2:0 subsampling bleeds colour across hard edges, which looks
     * like ringing/aliasing on this UI. Quality 95 keeps the wire small while
     * the chroma fix does the visible work. (True lossless = PNG; the encoder
     * is a follow-up because it needs a new dependency in the host build.) */
    cinfo.comp_info[0].h_samp_factor = 1;
    cinfo.comp_info[0].v_samp_factor = 1;
    jpeg_set_quality(&cinfo, 95, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = (JSAMPROW)(const uint8_t *)argb +
                       (size_t)cinfo.next_scanline * (size_t)w * 4;
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    return *out != NULL;
}

static void broadcast_frame(void)
{
    int nws = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i].fd >= 0 && s_clients[i].ws) nws++;
    }
    if (!nws) {
        return;
    }
    if (hal_display_snapshot(s_argb) != 0) {
        return;
    }
    /* The screen is usually static: only encode when it actually changed (or
     * a client just connected). Turns ~750 KB/s of idle video into silence. */
    size_t bytes = (size_t)s_win_w * (size_t)s_win_h * 4;
    if (!s_force_frame && s_have_prev && s_prev &&
        memcmp(s_prev, s_argb, bytes) == 0) {
        return;
    }
    s_force_frame = false;

    unsigned char *jpeg = NULL;
    unsigned long len = 0;
    if (!encode_jpeg(s_argb, s_win_w, s_win_h, &jpeg, &len)) {
        return;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_t *c = &s_clients[i];
        if (c->fd >= 0 && c->ws) {
            if (ws_send_frame(c->fd, jpeg, len, 0x2) != 0) {
                c->dead = true;
            }
        }
    }
    if (s_prev) {
        memcpy(s_prev, s_argb, bytes);
        s_have_prev = true;
    }
    free(jpeg);
}

/* ---- thread ------------------------------------------------------ */

static void *server_thread(void *arg)
{
    (void)arg;
    uint64_t next_frame = now_ms();
    const uint64_t interval = (uint64_t)(1000 / (s_fps > 0 ? s_fps : 18));

    while (s_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = s_listen;
        FD_SET(s_listen, &rfds);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (s_clients[i].fd >= 0) {
                FD_SET(s_clients[i].fd, &rfds);
                if (s_clients[i].fd > maxfd) maxfd = s_clients[i].fd;
            }
        }
        struct timeval tv = { 0, 4000 };   /* 4 ms */
        int rc = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0 && errno != EINTR) {
            break;
        }

        if (rc > 0 && FD_ISSET(s_listen, &rfds)) {
            struct sockaddr_in sa;
            socklen_t sl = sizeof sa;
            int fd = accept(s_listen, (struct sockaddr *)&sa, &sl);
            if (fd >= 0) {
                int one = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                struct timeval snd = { 1, 0 };
                setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd, sizeof snd);
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (s_clients[i].fd < 0) { slot = i; break; }
                }
                if (slot < 0) {
                    close(fd);
                } else {
                    s_clients[slot].fd = fd;
                    s_clients[slot].ws = false;
                    s_clients[slot].dead = false;
                    s_clients[slot].rxlen = 0;
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            client_t *c = &s_clients[i];
            if (c->fd < 0) continue;
            if (rc > 0 && FD_ISSET(c->fd, &rfds)) {
                handle_client_read(c);
            }
            if (c->dead) {
                close(c->fd);
                c->fd = -1;
                c->ws = false;
                c->rxlen = 0;
            }
        }

        flush_releases();

        uint64_t t = now_ms();
        if (t >= next_frame) {
            broadcast_frame();
            next_frame = t + interval;
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s_clients[i].fd >= 0) {
            close(s_clients[i].fd);
            s_clients[i].fd = -1;
        }
    }
    return NULL;
}

/* ---- public API -------------------------------------------------- */

int frame_server_start(const char *bind_ip, int port, int fps)
{
    if (port <= 0) {
        port = DEFAULT_PORT;
    }
    s_fps = fps > 0 ? fps : 18;

    hal_display_window_size(&s_win_w, &s_win_h);
    if (s_win_w <= 0 || s_win_h <= 0) {
        return 0;
    }
    size_t bytes = (size_t)s_win_w * (size_t)s_win_h * 4;
    s_argb = malloc(bytes);
    s_prev = malloc(bytes);
    if (!s_argb || !s_prev) {
        free(s_argb);
        free(s_prev);
        s_argb = NULL;
        s_prev = NULL;
        return 0;
    }
    s_have_prev = false;
    s_force_frame = true;

    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen < 0) {
        free(s_argb);
        s_argb = NULL;
        return 0;
    }
    int one = 1;
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (!bind_ip || !bind_ip[0] || !strcmp(bind_ip, "0.0.0.0")) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "[frames] bad bind address %s\n", bind_ip);
        close(s_listen);
        s_listen = -1;
        free(s_argb);
        free(s_prev);
        s_argb = NULL;
        s_prev = NULL;
        return 0;
    }

    /* First free port in a small range, so a busy port is not fatal. */
    int bound = 0;
    for (int i = 0; i < PORT_TRIES; i++) {
        addr.sin_port = htons((uint16_t)(port + i));
        if (bind(s_listen, (struct sockaddr *)&addr, sizeof addr) == 0) {
            bound = port + i;
            break;
        }
    }
    if (!bound || listen(s_listen, 4) != 0) {
        fprintf(stderr, "[frames] cannot bind %s:%d (%s)\n",
                bind_ip ? bind_ip : "0.0.0.0", port, strerror(errno));
        close(s_listen);
        s_listen = -1;
        free(s_argb);
        free(s_prev);
        s_argb = NULL;
        s_prev = NULL;
        return 0;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        s_clients[i].fd = -1;
        s_clients[i].ws = false;
        s_clients[i].dead = false;
        s_clients[i].rxlen = 0;
    }
    memset(s_pending, 0, sizeof s_pending);

    s_port = bound;
    s_running = true;
    if (pthread_create(&s_thread, NULL, server_thread, NULL) != 0) {
        s_running = false;
        close(s_listen);
        s_listen = -1;
        free(s_argb);
        s_argb = NULL;
        return 0;
    }
    return bound;
}

void frame_server_stop(void)
{
    if (!s_running) {
        return;
    }
    s_running = false;
    pthread_join(s_thread, NULL);
    if (s_listen >= 0) {
        close(s_listen);
        s_listen = -1;
    }
    free(s_argb);
    free(s_prev);
    s_argb = NULL;
    s_prev = NULL;
    s_have_prev = false;
    s_port = 0;
}
