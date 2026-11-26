/*
 * hal_storage.h — persistent filesystem for app data, settings and caches.
 * Declarations only. Backends later: SPIFFS/FAT on the S3 partition table;
 * a plain host directory (or tmpfs) in the native simulator.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_STORAGE_SYSTEM = 0,  /* read-only-ish system partition */
    HAL_STORAGE_USER,        /* user data, app state */
} hal_storage_part_t;

int  hal_storage_init(void);

/* Whole-file helpers (paths are absolute within the partition). */
int  hal_storage_read_file(hal_storage_part_t part, const char *path,
                           void *buf, size_t buf_len, size_t *out_len);
int  hal_storage_write_file(hal_storage_part_t part, const char *path,
                            const void *buf, size_t len);
int  hal_storage_remove(hal_storage_part_t part, const char *path);

/* Free/total bytes in the partition. */
int  hal_storage_info(hal_storage_part_t part, size_t *free_bytes, size_t *total_bytes);

#ifdef __cplusplus
}
#endif
