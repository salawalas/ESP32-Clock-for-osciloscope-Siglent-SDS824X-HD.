#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Konfiguracja trzymana w NVS jako pary klucz=tekst. Zmiana: komenda `set` w konsoli szeregowej. */

void        cfg_load(void);                               /* domyslne + wartosci z NVS */
const char *cfg_get(const char *key);                     /* NULL gdy nieznany klucz */
uint32_t    cfg_get_u32(const char *key);                 /* wartosc liczbowa (0 gdy blad) */
esp_err_t   cfg_set(const char *key, const char *value);  /* waliduje i zapisuje do NVS */
esp_err_t   cfg_reset(void);                              /* kasuje NVS i wraca do domyslnych */
void        cfg_dump(void);                               /* wypisuje wszystko (haslo zamaskowane) */

/* "a.b.c.d" -> adres w kolejnosci sieciowej (jak w lwIP). */
bool        ip4_parse(const char *s, uint32_t *addr_net);
