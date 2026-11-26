/*
 * hal_stubs.c — desktop stubs for the HAL interfaces that have no Tier-1
 * backend yet (net, storage, audio). They exist so the whole phone OS links
 * and runs on the host; the real backends land with the ESP32-S3 port.
 *
 * Storage is the one stub with real behavior: it is backed by a plain host
 * directory (./data) so app-state code can be exercised natively.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "hal/hal_audio.h"
#include "hal/hal_net.h"
#include "hal/hal_storage.h"

/* ---- net (stub) --------------------------------------------------- */

int hal_net_init(void) { return 0; }

hal_net_state_t hal_net_state(void) { return HAL_NET_DISCONNECTED; }

int hal_net_tcp_connect(const char *host, uint16_t port)
{
    (void)host; (void)port;
    return -1;   /* not wired yet */
}

int hal_net_tcp_send(int fd, const void *buf, size_t len)
{
    (void)fd; (void)buf; (void)len;
    return -1;
}

int hal_net_tcp_recv(int fd, void *buf, size_t len)
{
    (void)fd; (void)buf; (void)len;
    return -1;
}

int hal_net_tcp_close(int fd) { (void)fd; return 0; }

/* ---- storage (host directory backend) ----------------------------- */

#define STORAGE_ROOT "data"

static const char *part_dir(hal_storage_part_t part)
{
    return part == HAL_STORAGE_SYSTEM ? STORAGE_ROOT "/system" : STORAGE_ROOT "/user";
}

int hal_storage_init(void)
{
    char path[256];
    snprintf(path, sizeof path, "%s/system", STORAGE_ROOT);
    mkdir(STORAGE_ROOT, 0755);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/user", STORAGE_ROOT);
    mkdir(path, 0755);
    return 0;
}

static void full_path(hal_storage_part_t part, const char *path, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/%s", part_dir(part), path);
}

int hal_storage_read_file(hal_storage_part_t part, const char *path,
                          void *buf, size_t buf_len, size_t *out_len)
{
    char fp[512];
    full_path(part, path, fp, sizeof fp);
    FILE *f = fopen(fp, "rb");
    if (!f) {
        return -1;
    }
    size_t n = fread(buf, 1, buf_len, f);
    fclose(f);
    if (out_len) {
        *out_len = n;
    }
    return 0;
}

int hal_storage_write_file(hal_storage_part_t part, const char *path,
                           const void *buf, size_t len)
{
    char fp[512];
    full_path(part, path, fp, sizeof fp);
    FILE *f = fopen(fp, "wb");
    if (!f) {
        return -1;
    }
    size_t n = fwrite(buf, 1, len, f);
    fclose(f);
    return n == len ? 0 : -1;
}

int hal_storage_remove(hal_storage_part_t part, const char *path)
{
    char fp[512];
    full_path(part, path, fp, sizeof fp);
    return remove(fp) == 0 ? 0 : -1;
}

int hal_storage_info(hal_storage_part_t part, size_t *free_bytes, size_t *total_bytes)
{
    (void)part;
    if (free_bytes)  *free_bytes = 0;
    if (total_bytes) *total_bytes = 0;
    return -1;   /* unknown on the stub backend */
}

/* ---- audio (silent stub) ------------------------------------------ */

int hal_audio_init(void) { return 0; }

int hal_audio_write(const void *pcm, size_t len_bytes)
{
    (void)pcm;
    return (int)len_bytes;   /* swallow silently: ringer is a no-op here */
}

int hal_audio_set_volume(uint8_t percent) { (void)percent; return 0; }

int hal_audio_capture_start(hal_audio_cfg_t cfg) { (void)cfg; return -1; }

int hal_audio_capture_stop(void) { return 0; }
