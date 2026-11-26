/*
 * hal_modem.h — cellular modem (UART AT or PPP). Prototype #1 has NO modem;
 * this interface exists so a simulated one can be plugged in via a virtual
 * UART in QEMU (see ../harness-setup.md, `sim sms ...`).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_MODEM_OFF = 0,
    HAL_MODEM_REGISTERING,
    HAL_MODEM_REGISTERED,
    HAL_MODEM_ERROR,
} hal_modem_state_t;

typedef struct {
    char number[32];
    char text[256];
    int64_t timestamp;
} hal_modem_sms_t;

int  hal_modem_init(void);

hal_modem_state_t hal_modem_state(void);

/* Send a raw AT command, wait for a final result code.
 * Reply buffer is NUL-terminated; returns 0 on OK, <0 on error/timeout. */
int  hal_modem_at(const char *cmd, char *reply, size_t reply_len, uint32_t timeout_ms);

/* Receive one SMS; returns true if an SMS was dequeued into *out. */
bool hal_modem_sms_read(hal_modem_sms_t *out);

#ifdef __cplusplus
}
#endif
