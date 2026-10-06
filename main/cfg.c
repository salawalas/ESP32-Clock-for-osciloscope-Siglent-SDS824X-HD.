#include "cfg.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "lwip/sockets.h"   /* AF_INET, inet_pton */
#include "nvs.h"

static const char *TAG = "cfg";
#define NVS_NS "zegar"

typedef enum { T_STR, T_IP, T_NUM } ctype_t;

typedef struct {
    const char *key;      /* max 15 znakow (limit NVS) */
    char       *buf;
    size_t      size;
    const char *def;
    ctype_t     type;
    bool        secret;
} item_t;

static char v_wifi_ssid[33];
static char v_wifi_pass[65];
static char v_wifi_ip[16];
static char v_wifi_mask[16];
static char v_wifi_gw[16];
static char v_wifi_dns[16];
static char v_wifi_boot[4];
static char v_wifi_timeout[8];
static char v_ntp_server[64];
static char v_tz[48];
static char v_eth_ip[16];
static char v_eth_mask[16];
static char v_scope_ip[16];
static char v_scope_port[8];
static char v_scope_date_fmt[48];
static char v_scope_time_fmt[48];
static char v_scope_resync[8];

#define ITEM(k, t, d, s) { #k, v_##k, sizeof(v_##k), d, t, s }

static const item_t s_items[] = {
    ITEM(wifi_ssid,       T_STR, "",                           false),
    ITEM(wifi_pass,       T_STR, "",                           true),
    ITEM(wifi_ip,         T_IP,  "192.168.1.50",               false),
    ITEM(wifi_mask,       T_IP,  "255.255.255.0",              false),
    ITEM(wifi_gw,         T_IP,  "192.168.1.1",                false),
    ITEM(wifi_dns,        T_IP,  "192.168.1.1",                false),
    ITEM(wifi_boot,       T_NUM, "1",                          false),  /* 1 = sync Wi-Fi/NTP przy kazdym starcie */
    ITEM(wifi_timeout,    T_NUM, "20",                         false),  /* s na polaczenie z Wi-Fi */
    ITEM(ntp_server,      T_STR, "pool.ntp.org",               false),  /* nazwa lub IP */
    ITEM(tz,              T_STR, "CET-1CEST,M3.5.0,M10.5.0/3", false),  /* POSIX TZ */
    ITEM(eth_ip,          T_IP,  "192.168.77.1",               false),
    ITEM(eth_mask,        T_IP,  "255.255.255.0",              false),
    ITEM(scope_ip,        T_IP,  "192.168.77.2",               false),
    ITEM(scope_port,      T_NUM, "5025",                       false),
    ITEM(scope_date_fmt,  T_STR, ":SYSTem:DATE %Y%m%d",        false),  /* strftime; wg Programming Guide: RRRRMMDD */
    ITEM(scope_time_fmt,  T_STR, ":SYSTem:TIME %H%M%S",        false),  /* strftime; wg Programming Guide: GGMMSS */
    ITEM(scope_resync,    T_NUM, "0",                          false),  /* co ile minut ponawiac wysylke (0 = tylko po starcie) */
};
#define N_ITEMS (sizeof(s_items) / sizeof(s_items[0]))

static const item_t *find(const char *key)
{
    for (size_t i = 0; i < N_ITEMS; i++)
        if (strcmp(s_items[i].key, key) == 0) return &s_items[i];
    return NULL;
}

bool ip4_parse(const char *s, uint32_t *addr_net)
{
    struct in_addr a;
    if (!s || inet_pton(AF_INET, s, &a) != 1) return false;
    *addr_net = a.s_addr;
    return true;
}

static void load_defaults(void)
{
    for (size_t i = 0; i < N_ITEMS; i++)
        strlcpy(s_items[i].buf, s_items[i].def, s_items[i].size);
}

void cfg_load(void)
{
    load_defaults();
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "brak zapisanej konfiguracji - domyslne wartosci");
        return;
    }
    for (size_t i = 0; i < N_ITEMS; i++) {
        size_t n = s_items[i].size;
        char tmp[96];
        if (n > sizeof(tmp)) n = sizeof(tmp);
        if (nvs_get_str(h, s_items[i].key, tmp, &n) == ESP_OK)
            strlcpy(s_items[i].buf, tmp, s_items[i].size);
    }
    nvs_close(h);
}

const char *cfg_get(const char *key)
{
    const item_t *it = find(key);
    return it ? it->buf : NULL;
}

uint32_t cfg_get_u32(const char *key)
{
    const char *s = cfg_get(key);
    return s ? (uint32_t)strtoul(s, NULL, 10) : 0;
}

esp_err_t cfg_set(const char *key, const char *val)
{
    const item_t *it = find(key);
    if (!it) return ESP_ERR_NOT_FOUND;
    if (strlen(val) >= it->size) return ESP_ERR_INVALID_SIZE;

    if (it->type == T_IP) {
        uint32_t a;
        if (!ip4_parse(val, &a)) return ESP_ERR_INVALID_ARG;
    } else if (it->type == T_NUM) {
        if (!*val) return ESP_ERR_INVALID_ARG;
        for (const char *p = val; *p; p++)
            if (!isdigit((unsigned char)*p)) return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_str(h, it->key, val);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e == ESP_OK) strlcpy(it->buf, val, it->size);
    return e;
}

esp_err_t cfg_reset(void)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_erase_all(h);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    load_defaults();
    return e;
}

void cfg_dump(void)
{
    for (size_t i = 0; i < N_ITEMS; i++) {
        const char *v = s_items[i].buf;
        if (s_items[i].secret) v = *v ? "********" : "";
        printf("%-16s = %s\n", s_items[i].key, v);
    }
}
