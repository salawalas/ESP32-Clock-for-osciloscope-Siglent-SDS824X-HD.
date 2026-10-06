#pragma once
#include <stddef.h>
#include "esp_err.h"

/* Zadanie, ktore czeka na EVT_PUSH_SCOPE i wysyla czas do oscyloskopu (z ponawianiem). */
void scope_start(void);
/* Jednorazowa wysylka czasu (SCPI po TCP). */
esp_err_t scope_push_time(void);
/* Surowa komenda SCPI; jesli zawiera '?', odczytuje odpowiedz do resp. */
esp_err_t scope_raw(const char *cmd, char *resp, size_t resp_sz);
