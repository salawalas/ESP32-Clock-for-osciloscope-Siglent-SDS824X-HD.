#include "scope.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "app.h"
#include "cfg.h"
#include "net_eth.h"
#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "scope";

#define PUSH_WINDOW_S   600     /* tyle czasu probujemy po starcie (oscyloskop bootuje sie dlugo) */
#define RETRY_MS        3000

static int scope_connect(void)
{
    uint32_t ip;
    if (!ip4_parse(cfg_get("scope_ip"), &ip)) return -1;
    struct sockaddr_in a = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)cfg_get_u32("scope_port")),
        .sin_addr.s_addr = ip,
    };

    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) return -1;

    /* connect() z krotkim limitem: nieblokujacy + select */
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
    int r = connect(s, (struct sockaddr *)&a, sizeof(a));
    if (r < 0 && errno != EINPROGRESS) { close(s); return -1; }
    if (r < 0) {
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(s, &wf);
        struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
        if (select(s + 1, NULL, &wf, NULL, &tv) <= 0) { close(s); return -1; }
        int err = 0;
        socklen_t l = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &l);
        if (err) { close(s); return -1; }
    }
    fcntl(s, F_SETFL, fl);

    struct timeval rt = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof(rt));
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return s;
}

static bool send_line(int s, const char *cmd)
{
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "%s\n", cmd);
    return n > 0 && n < (int)sizeof(buf) && send(s, buf, n, 0) == n;
}

static int recv_line(int s, char *out, size_t sz)
{
    size_t n = 0;
    while (n < sz - 1) {
        char c;
        int r = recv(s, &c, 1, 0);
        if (r <= 0) break;                          /* timeout lub zamkniecie */
        if (c == '\n') break;
        if (c != '\r') out[n++] = c;
    }
    out[n] = 0;
    return (int)n;
}

esp_err_t scope_raw(const char *cmd, char *resp, size_t resp_sz)
{
    if (resp && resp_sz) resp[0] = 0;
    int s = scope_connect();
    if (s < 0) return ESP_FAIL;
    esp_err_t e = send_line(s, cmd) ? ESP_OK : ESP_FAIL;
    if (e == ESP_OK && strchr(cmd, '?') && resp && resp_sz) {
        if (recv_line(s, resp, resp_sz) == 0) e = ESP_ERR_TIMEOUT;
    }
    close(s);
    return e;
}

esp_err_t scope_push_time(void)
{
    char dcmd[96], tcmd[96], resp[96];
    time_t now;
    struct tm lt;

    int s = scope_connect();
    if (s < 0) return ESP_FAIL;

    if (send_line(s, "*IDN?") && recv_line(s, resp, sizeof(resp)) > 0)
        ESP_LOGI(TAG, "urzadzenie: %s", resp);

    /* Wysylamy czas lokalny (jak na ekranie oscyloskopu). Czas liczony tuz przed wyslaniem. */
    now = time(NULL);
    localtime_r(&now, &lt);
    if (!strftime(dcmd, sizeof(dcmd), cfg_get("scope_date_fmt"), &lt)) { close(s); return ESP_ERR_INVALID_SIZE; }
    bool ok = send_line(s, dcmd);
    ESP_LOGI(TAG, "> %s", dcmd);

    now = time(NULL);
    localtime_r(&now, &lt);
    if (!strftime(tcmd, sizeof(tcmd), cfg_get("scope_time_fmt"), &lt)) { close(s); return ESP_ERR_INVALID_SIZE; }
    ok = ok && send_line(s, tcmd);
    ESP_LOGI(TAG, "> %s", tcmd);

    /* Odczyt kontrolny - w logu zobaczysz, co oscyloskop faktycznie przyjal */
    vTaskDelay(pdMS_TO_TICKS(300));
    if (send_line(s, ":SYSTem:DATE?") && recv_line(s, resp, sizeof(resp)) > 0)
        ESP_LOGI(TAG, "oscyloskop, data:  %s", resp);
    if (send_line(s, ":SYSTem:TIME?") && recv_line(s, resp, sizeof(resp)) > 0)
        ESP_LOGI(TAG, "oscyloskop, czas:  %s", resp);

    close(s);
    return ok ? ESP_OK : ESP_FAIL;
}

static void scope_task(void *arg)
{
    for (;;) {
        uint32_t resync_min = cfg_get_u32("scope_resync");
        TickType_t wait = resync_min ? pdMS_TO_TICKS(resync_min * 60000UL) : portMAX_DELAY;
        xEventGroupWaitBits(g_evt, EVT_PUSH_SCOPE, pdTRUE, pdFALSE, wait);   /* zadanie albo okresowe odswiezenie */

        if (!g_time_valid) {
            ESP_LOGW(TAG, "brak poprawnego czasu (ustaw: 'settime' lub 'sync') - nie wysylam");
            continue;
        }

        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(PUSH_WINDOW_S * 1000);
        bool done = false;
        while ((int32_t)(deadline - xTaskGetTickCount()) > 0) {
            if (eth_link_up()) {
                esp_err_t e = scope_push_time();
                if (e == ESP_OK) {
                    ESP_LOGI(TAG, "czas wyslany do oscyloskopu");
                    done = true;
                    break;
                }
                ESP_LOGW(TAG, "oscyloskop nie odpowiada na %s:%u (%s) - ponawiam",
                         cfg_get("scope_ip"), (unsigned)cfg_get_u32("scope_port"), esp_err_to_name(e));
            } else {
                ESP_LOGW(TAG, "brak linku Ethernet - czekam");
            }
            vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
        }
        if (!done)
            ESP_LOGW(TAG, "poddaje sie; nacisnij BOOT albo wpisz 'push', gdy oscyloskop bedzie gotowy");
    }
}

void scope_start(void)
{
    xTaskCreate(scope_task, "scope_push", 4096, NULL, 5, NULL);
}
