#include "net_wifi.h"
#include <string.h>
#include "app.h"
#include "cfg.h"
#include "rtc_ds3231.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_sntp.h"

static const char *TAG = "wifi";

#define W_UP BIT0

static EventGroupHandle_t s_ev;
static esp_netif_t *s_sta;
static bool s_inited;
static volatile bool s_active;

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            if (s_active) esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_CONNECTED) {
            xEventGroupSetBits(s_ev, W_UP);
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            xEventGroupClearBits(s_ev, W_UP);
            if (s_active) esp_wifi_connect();       /* ponawiaj do konca okna czasowego */
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_ev, W_UP);
    }
}

static esp_err_t wifi_init_once(void)
{
    if (s_inited) return ESP_OK;
    s_ev = xEventGroupCreate();
    s_sta = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&cfg);
    if (e != ESP_OK) return e;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL);
    s_inited = true;
    return ESP_OK;
}

static esp_err_t apply_static_ip(void)
{
    uint32_t ip, mask, gw, dns;
    if (!ip4_parse(cfg_get("wifi_ip"), &ip) || !ip4_parse(cfg_get("wifi_mask"), &mask) ||
        !ip4_parse(cfg_get("wifi_gw"), &gw) || !ip4_parse(cfg_get("wifi_dns"), &dns)) {
        ESP_LOGE(TAG, "zle wifi_ip / wifi_mask / wifi_gw / wifi_dns");
        return ESP_ERR_INVALID_ARG;
    }
    esp_netif_dhcpc_stop(s_sta);                    /* ESP_ERR_..._ALREADY_STOPPED jest OK */
    esp_netif_ip_info_t info = { 0 };
    info.ip.addr = ip;
    info.netmask.addr = mask;
    info.gw.addr = gw;
    esp_err_t e = esp_netif_set_ip_info(s_sta, &info);
    if (e != ESP_OK) return e;

    esp_netif_dns_info_t d = { 0 };
    d.ip.type = ESP_IPADDR_TYPE_V4;
    d.ip.u_addr.ip4.addr = dns;
    return esp_netif_set_dns_info(s_sta, ESP_NETIF_DNS_MAIN, &d);
}

esp_err_t wifi_sync_time(void)
{
    const char *ssid = cfg_get("wifi_ssid");
    if (!ssid || !ssid[0]) {
        ESP_LOGW(TAG, "wifi_ssid nie ustawione - pomijam");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = wifi_init_once();
    if (err != ESP_OK) return err;
    err = apply_static_ip();
    if (err != ESP_OK) return err;

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, cfg_get("wifi_pass"), sizeof(wc.sta.password));
    wc.sta.threshold.authmode = wc.sta.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_STA, &wc);

    xEventGroupClearBits(s_ev, W_UP);
    s_active = true;
    err = esp_wifi_start();
    if (err != ESP_OK) { s_active = false; return err; }

    uint32_t to = cfg_get_u32("wifi_timeout");
    ESP_LOGI(TAG, "lacze z \"%s\" (IP %s), limit %u s", ssid, cfg_get("wifi_ip"), (unsigned)to);
    EventBits_t b = xEventGroupWaitBits(s_ev, W_UP, pdFALSE, pdFALSE, pdMS_TO_TICKS(to * 1000));
    if (!(b & W_UP)) {
        ESP_LOGW(TAG, "brak polaczenia z Wi-Fi w limicie czasu");
        err = ESP_ERR_TIMEOUT;
        goto out;
    }
    vTaskDelay(pdMS_TO_TICKS(300));                 /* niech netif skonczy sie podnosic */
    esp_netif_set_default_netif(s_sta);

    const char *ntp = cfg_get("ntp_server");
    ESP_LOGI(TAG, "pytam serwer NTP: %s", ntp);
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp);
    err = esp_netif_sntp_init(&sc);
    if (err == ESP_OK) {
        err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
        esp_netif_sntp_deinit();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "synchronizacja NTP nieudana (%s)", esp_err_to_name(err));
        goto out;
    }

    time_t now = time(NULL);                        /* SNTP ustawilo zegar systemowy (UTC) */
    if (now < TIME_MIN_VALID) {
        ESP_LOGW(TAG, "NTP zwrocil niewiarygodny czas");
        err = ESP_ERR_INVALID_RESPONSE;
        goto out;
    }
    g_time_valid = true;
    esp_err_t re = ds3231_set_utc(now);
    if (re != ESP_OK) ESP_LOGW(TAG, "zapis do DS3231 nieudany (%s)", esp_err_to_name(re));
    else ESP_LOGI(TAG, "czas z NTP zapisany do DS3231");
    xEventGroupSetBits(g_evt, EVT_PUSH_SCOPE);      /* odswiez czas w oscyloskopie */

out:
    s_active = false;
    esp_wifi_disconnect();
    esp_wifi_stop();                                /* Wi-Fi wylaczone - mniejszy pobor z USB oscyloskopu */
    return err;
}

static void wifi_task(void *arg)
{
    for (;;) {
        xEventGroupWaitBits(g_evt, EVT_WIFI_SYNC, pdTRUE, pdFALSE, portMAX_DELAY);
        esp_err_t e = wifi_sync_time();
        ESP_LOGI(TAG, "sync Wi-Fi/NTP: %s", e == ESP_OK ? "OK" : esp_err_to_name(e));
    }
}

void wifi_sync_start(void)
{
    xTaskCreate(wifi_task, "wifi_sync", 6144, NULL, 4, NULL);
}
