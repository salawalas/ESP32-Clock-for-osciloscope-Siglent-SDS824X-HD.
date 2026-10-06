#pragma once
/* Czyste funkcje bez zaleznosci od ESP-IDF (mozna testowac na PC). */
#include <stdint.h>
#include <time.h>

/* Dni od 1970-01-01 dla daty w kalendarzu gregorianskim (algorytm H. Hinnanta). */
static inline int64_t tu_days_from_civil(int y, int m, int d)
{
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int mp  = (m + 9) % 12;                 /* marzec=0 ... luty=11 */
    const int doy = (153 * mp + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

/* UTC (rok, mies, dzien, godz, min, sek) -> epoch. Bez zaleznosci od TZ i timegm(). */
static inline time_t tu_epoch(int y, int mo, int d, int h, int mi, int s)
{
    return (time_t)(tu_days_from_civil(y, mo, d) * 86400LL + h * 3600 + mi * 60 + s);
}

static inline uint8_t tu_bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static inline uint8_t tu_bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
