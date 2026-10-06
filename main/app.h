#pragma once
#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* Bity zdarzen miedzy zadaniami */
#define EVT_PUSH_SCOPE  (1u << 0)   /* wyslij czas do oscyloskopu */
#define EVT_WIFI_SYNC   (1u << 1)   /* pobierz czas z NTP przez Wi-Fi */

/* 2025-01-01 00:00:00 UTC - wszystko wczesniejsze uznajemy za niepoprawny czas */
#define TIME_MIN_VALID  1735689600

extern EventGroupHandle_t g_evt;
extern volatile bool g_time_valid;

/* Ustawia zegar systemowy (UTC) i oznacza czas jako poprawny. */
void app_set_system_utc(time_t utc);
/* Stosuje strefe czasowa z konfiguracji (klucz "tz"). */
void app_apply_tz(void);
