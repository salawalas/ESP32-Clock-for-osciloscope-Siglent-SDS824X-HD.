#pragma once
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

/* DS3231 przechowuje czas UTC (24h). Strefa/DST liczone sa w oprogramowaniu. */

esp_err_t ds3231_init(int sda, int scl);
/* osf=true: uklad zgubil zasilanie/zatrzymal oscylator - czas niewiarygodny do czasu ponownego ustawienia */
esp_err_t ds3231_get_utc(time_t *utc, bool *osf);
/* zapisuje czas i kasuje flage OSF */
esp_err_t ds3231_set_utc(time_t utc);
