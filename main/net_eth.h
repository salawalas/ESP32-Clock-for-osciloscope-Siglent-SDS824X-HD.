#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* W5500 po SPI, staly IP, polaczenie kablem prosto z oscyloskopem. */
esp_err_t eth_start(void);
bool      eth_link_up(void);
