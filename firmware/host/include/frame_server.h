/*
 * frame_server.h — browser viewport for the desktop simulator (host-only).
 *
 * Serves the COMPOSED window (panel + bezel, so the physical keys are visible
 * and tappable) as JPEG frames over a WebSocket, plus a small index.html on
 * GET /. Input arrives as JSON on the same socket:
 *
 *     {"tap":true,"x":N,"y":N}   window coordinates -> synthesised mouse click
 *     {"key":"digit-5"}          a bezel key id, or home/back/menu
 *
 * Taps are pushed into SDL's event queue, so they go through exactly the same
 * hit test as a real mouse click; named keys resolve through the bezel table
 * (lighting the cap up) or straight into the HAL input queue.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Start serving. bind_ip NULL = 0.0.0.0; port <= 0 = first free port from the
 * default. Returns the bound port (0 on failure). */
int  frame_server_start(const char *bind_ip, int port, int fps);

/* Stop the thread and close all sockets. */
void frame_server_stop(void);

#ifdef __cplusplus
}
#endif
