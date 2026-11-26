/*
 * hal_net.h — network interface (Prototype #1: WiFi-only, no cellular).
 * Declarations only; backends come later: esp_wifi/lwIP on the S3, plain
 * POSIX sockets on the host simulator.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_NET_DISCONNECTED = 0,
    HAL_NET_CONNECTING,
    HAL_NET_CONNECTED,
} hal_net_state_t;

int  hal_net_init(void);

hal_net_state_t hal_net_state(void);

/* Open a TCP connection to host:port. Returns fd >= 0 on success, <0 on error. */
int  hal_net_tcp_connect(const char *host, uint16_t port);
int  hal_net_tcp_send(int fd, const void *buf, size_t len);
int  hal_net_tcp_recv(int fd, void *buf, size_t len);
int  hal_net_tcp_close(int fd);

#ifdef __cplusplus
}
#endif
