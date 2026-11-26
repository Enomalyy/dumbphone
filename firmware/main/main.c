/*
 * dumbphone firmware — minimal ESP-IDF scaffold app.
 *
 * Phase 1: boots, prints a banner on UART0, and runs one FreeRTOS task that
 * ticks a counter. No HAL backends, no LVGL, no OS shell yet.
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "dumbphone";

static void heartbeat_task(void *arg)
{
    (void)arg;
    uint32_t tick = 0;
    while (true) {
        ESP_LOGI(TAG, "alive tick=%lu heap=%lu", (unsigned long)tick++,
                 (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    ESP_LOGI(TAG, "dumbphone scaffold booting");
    ESP_LOGI(TAG, "chip: %d core(s), silicon rev %d.%d",
             chip.cores, chip.revision / 100, chip.revision % 100);

    xTaskCreate(heartbeat_task, "heartbeat", 3072, NULL, 5, NULL);
}
