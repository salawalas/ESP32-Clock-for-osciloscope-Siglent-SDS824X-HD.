#include <stdlib.h>
#include <sys/time.h>
#include "app.h"
#include "cfg.h"
#include "cli.h"
#include "net_eth.h"
#include "net_wifi.h"
#include "pins.h"
#include "rtc_ds3231.h"
#include "scope.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "main";

EventGroupHandle_t g_evt;
volatile bool g_time_valid;

void app_set_system_utc(time_t utc)
{
    struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    g_time_valid = true;
}

void app_apply_tz(void)
{
    setenv("TZ", cfg_get("tz"), 1);
    tzset();
}

/* Przycisk BOOT: wymusza ponowna wysylke czasu (np. po restarcie samego oscyloskopu) */
static void button_task(void *arg)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_BTN_PUSH,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    bool prev = true;
    for (;;) {
        bool lvl = gpio_get_level(PIN_BTN_PUSH);
        if (prev && !lvl) {
            vTaskDelay(pdMS_TO_TICKS(30));                    /* debounce */
            if (!gpio_get_level(PIN_BTN_PUSH)) {
                ESP_LOGI(TAG, "przycisk BOOT -> wysylam czas do oscyloskopu");
                xEventGroupSetBits(g_evt, EVT_PUSH_SCOPE);
            }
        }
        prev = lvl;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    g_evt = xEventGroupCreate();
    cfg_load();
    app_apply_tz();

    /* 1. Czas z DS3231 (dziala bez zadnej sieci) */
    if (ds3231_init(PIN_I2C_SDA, PIN_I2C_SCL) == ESP_OK) {
        time_t t;
        bool osf;
        if (ds3231_get_utc(&t, &osf) == ESP_OK) {
            if (!osf && t >= TIME_MIN_VALID) {
                app_set_system_utc(t);
                ESP_LOGI(TAG, "czas z DS3231 poprawny");
            } else {
                ESP_LOGW(TAG, "DS3231: czas niewiarygodny (OSF/data) - potrzebna synchronizacja ('sync' / 'settime')");
            }
        }
    } else {
        ESP_LOGE(TAG, "DS3231 niedostepny - sprawdz SDA/SCL; czas tylko z NTP");
    }

    /* 2. Ethernet do oscyloskopu */
    if (eth_start() != ESP_OK)
        ESP_LOGE(TAG, "Ethernet (W5500) nie wystartowal - sprawdz okablowanie i menuconfig");

    /* 3. Zadania */
    scope_start();
    wifi_sync_start();
    xTaskCreate(button_task, "button", 2048, NULL, 3, NULL);
    cli_start();

    /* 4. Pierwsze akcje po starcie */
    if (g_time_valid) xEventGroupSetBits(g_evt, EVT_PUSH_SCOPE);
    if (cfg_get_u32("wifi_boot") && cfg_get("wifi_ssid")[0]) xEventGroupSetBits(g_evt, EVT_WIFI_SYNC);
}
