#pragma once
#include "esp_err.h"

/* Uruchamia zadanie synchronizacji (czeka na EVT_WIFI_SYNC). */
void wifi_sync_start(void);
/* Jednorazowo: Wi-Fi (staly IP) -> NTP -> zegar systemowy + DS3231. Blokuje do konca proby. */
esp_err_t wifi_sync_time(void);
