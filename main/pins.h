#pragma once
/* Piny dla ESP32 DevKit C (ESP32-WROOM-32). Zmien tutaj, jesli podlaczysz inaczej. */

/* DS3231 (I2C) */
#define PIN_I2C_SDA     21
#define PIN_I2C_SCL     22

/* W5500 (SPI) - VSPI, natywne piny IOMUX */
#define ETH_SPI_HOST    SPI3_HOST
#define ETH_SPI_MHZ     10          /* 10 MHz jest bezpieczne na kabelkach; na krotkich polaczeniach mozna 20 */
#define PIN_ETH_SCLK    18
#define PIN_ETH_MISO    19
#define PIN_ETH_MOSI    23
#define PIN_ETH_CS      5           /* na module W5500 opisany jako SCS */
#define PIN_ETH_INT     4
#define PIN_ETH_RST     16          /* WROVER: 16/17 zajete przez PSRAM - wtedy zmien */

/* Przycisk BOOT na DevKit C: krotkie nacisniecie = wyslij czas do oscyloskopu */
#define PIN_BTN_PUSH    0
