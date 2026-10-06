#include "cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "app.h"
#include "cfg.h"
#include "net_eth.h"
#include "rtc_ds3231.h"
#include "scope.h"
#include "esp_console.h"
#include "esp_system.h"

static void fmt_local(time_t t, char *out, size_t n)
{
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(out, n, "%Y-%m-%d %H:%M:%S %Z", &lt);
}

static void fmt_utc(time_t t, char *out, size_t n)
{
    struct tm gt;
    gmtime_r(&t, &gt);
    strftime(out, n, "%Y-%m-%d %H:%M:%S UTC", &gt);
}

static void print_times(void)
{
    char a[48], b[48];
    time_t now = time(NULL);
    fmt_utc(now, a, sizeof(a));
    fmt_local(now, b, sizeof(b));
    printf("zegar systemowy : %s | %s | %s\n", a, b, g_time_valid ? "poprawny" : "NIEPOPRAWNY");

    time_t rtc;
    bool osf;
    if (ds3231_get_utc(&rtc, &osf) == ESP_OK) {
        fmt_utc(rtc, a, sizeof(a));
        fmt_local(rtc, b, sizeof(b));
        printf("DS3231          : %s | %s%s\n", a, b, osf ? " | OSF (czas niewiarygodny - ustaw go)" : "");
    } else {
        printf("DS3231          : brak odpowiedzi\n");
    }
}

static int cmd_show(int argc, char **argv)
{
    cfg_dump();
    printf("--\n");
    print_times();
    printf("link Ethernet   : %s\n", eth_link_up() ? "UP" : "DOWN");
    return 0;
}

static int cmd_time(int argc, char **argv)
{
    print_times();
    return 0;
}

static int cmd_set(int argc, char **argv)
{
    if (argc < 3) {
        printf("uzycie: set <klucz> <wartosc>   (klucze: 'show'; wartosci ze spacjami w cudzyslowie)\n");
        return 1;
    }
    char val[96] = "";
    for (int i = 2; i < argc; i++) {
        if (i > 2) strlcat(val, " ", sizeof(val));
        strlcat(val, argv[i], sizeof(val));
    }
    esp_err_t e = cfg_set(argv[1], val);
    switch (e) {
    case ESP_OK:
        printf("OK\n");
        if (strcmp(argv[1], "tz") == 0) app_apply_tz();
        return 0;
    case ESP_ERR_NOT_FOUND:    printf("nieznany klucz (wpisz 'show')\n"); break;
    case ESP_ERR_INVALID_SIZE: printf("wartosc za dluga\n"); break;
    case ESP_ERR_INVALID_ARG:  printf("niepoprawna wartosc (IP: a.b.c.d, liczby: same cyfry)\n"); break;
    default:                   printf("blad zapisu NVS: %s\n", esp_err_to_name(e)); break;
    }
    return 1;
}

static int cmd_settime(int argc, char **argv)
{
    int Y, M, D, h, m, s;
    if (argc != 3 || sscanf(argv[1], "%d-%d-%d", &Y, &M, &D) != 3 || sscanf(argv[2], "%d:%d:%d", &h, &m, &s) != 3) {
        printf("uzycie: settime RRRR-MM-DD GG:MM:SS   (czas lokalny wg 'tz')\n");
        return 1;
    }
    struct tm tm = { .tm_year = Y - 1900, .tm_mon = M - 1, .tm_mday = D,
                     .tm_hour = h, .tm_min = m, .tm_sec = s, .tm_isdst = -1 };
    time_t t = mktime(&tm);
    if (t < TIME_MIN_VALID) { printf("data poza zakresem (od 2025-01-01)\n"); return 1; }

    app_set_system_utc(t);
    esp_err_t e = ds3231_set_utc(t);
    printf("zegar systemowy ustawiony; DS3231: %s\n", e == ESP_OK ? "OK" : esp_err_to_name(e));
    xEventGroupSetBits(g_evt, EVT_PUSH_SCOPE);
    return 0;
}

static int cmd_sync(int argc, char **argv)
{
    xEventGroupSetBits(g_evt, EVT_WIFI_SYNC);
    printf("synchronizacja Wi-Fi/NTP uruchomiona - wynik w logu\n");
    return 0;
}

static int cmd_push(int argc, char **argv)
{
    xEventGroupSetBits(g_evt, EVT_PUSH_SCOPE);
    printf("wysylka czasu do oscyloskopu zlecona - wynik w logu\n");
    return 0;
}

static int cmd_scope(int argc, char **argv)
{
    if (argc < 2) {
        printf("uzycie: scope <komenda SCPI>   np.  scope *IDN?   |   scope :SYSTem:DATE?\n");
        return 1;
    }
    char cmd[128] = "", resp[160];
    for (int i = 1; i < argc; i++) {
        if (i > 1) strlcat(cmd, " ", sizeof(cmd));
        strlcat(cmd, argv[i], sizeof(cmd));
    }
    esp_err_t e = scope_raw(cmd, resp, sizeof(resp));
    if (e == ESP_OK) printf("%s\n", strchr(cmd, '?') ? resp : "wyslano");
    else if (e == ESP_ERR_TIMEOUT) printf("wyslano, ale brak odpowiedzi\n");
    else printf("nie mozna polaczyc z oscyloskopem (%s:%u)\n", cfg_get("scope_ip"), (unsigned)cfg_get_u32("scope_port"));
    return e == ESP_OK ? 0 : 1;
}

static int cmd_factory(int argc, char **argv)
{
    esp_err_t e = cfg_reset();
    printf("konfiguracja przywrocona do domyslnej: %s\n", e == ESP_OK ? "OK" : esp_err_to_name(e));
    app_apply_tz();
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    esp_restart();
    return 0;
}

void cli_start(void)
{
    static const esp_console_cmd_t cmds[] = {
        { .command = "show",    .help = "pokaz konfiguracje, czas i stan lacza",              .func = cmd_show },
        { .command = "time",    .help = "pokaz czas (system i DS3231)",                       .func = cmd_time },
        { .command = "set",     .help = "set <klucz> <wartosc> - zapisz ustawienie w NVS",    .func = cmd_set },
        { .command = "settime", .help = "settime RRRR-MM-DD GG:MM:SS - reczne ustawienie czasu lokalnego", .func = cmd_settime },
        { .command = "sync",    .help = "pobierz czas z NTP przez Wi-Fi i zapisz do DS3231",  .func = cmd_sync },
        { .command = "push",    .help = "wyslij czas do oscyloskopu teraz",                   .func = cmd_push },
        { .command = "scope",   .help = "scope <SCPI> - surowa komenda do oscyloskopu",       .func = cmd_scope },
        { .command = "factory", .help = "przywroc domyslna konfiguracje",                     .func = cmd_factory },
        { .command = "reboot",  .help = "restart",                                            .func = cmd_reboot },
    };

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "zegar> ";
    rc.max_cmdline_length = 256;
    esp_console_dev_uart_config_t uc = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uc, &rc, &repl));

    esp_console_register_help_command();
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
