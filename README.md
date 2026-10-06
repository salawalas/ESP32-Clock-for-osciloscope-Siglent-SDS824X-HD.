# Zegar RTC dla Siglent SDS800X HD (ESP32 + DS3231 + W5500)

Pudełko, które po włączeniu wysyła do oscyloskopu aktualną datę i godzinę (SCPI po TCP, port 5025).
Czas pochodzi z DS3231 (działa bez sieci), a gdy w zasięgu jest skonfigurowane Wi-Fi, jest odświeżany z NTP.

## Połączenia (ESP32 DevKit C)

| Moduł  | Pin modułu | GPIO ESP32 |
|--------|-----------|-----------|
| DS3231 | SDA / SCL | 21 / 22   |
| W5500  | SCLK      | 18        |
|        | MISO      | 19        |
|        | MOSI      | 23        |
|        | SCS       | 5         |
|        | INT       | 4         |
|        | RST       | 16        |
| oba    | zasilanie, GND | 3V3 (lub 5V wg opisu na module W5500 - tylko jedno z nich), GND |

Piny zmienisz w `main/pins.h`. Kabel Ethernet idzie prosto z W5500 do portu LAN oscyloskopu.
Przycisk BOOT na płytce = "wyślij czas do oscyloskopu teraz".

## Ustawienia w oscyloskopie

LAN: stały adres IP `192.168.77.2`, maska `255.255.255.0`, brama pusta. Wyłącz w menu daty/czasu
synchronizację NTP przy starcie, żeby nie nadpisywała czasu z pudełka.

## Budowanie

ESP-IDF >= 5.3 (testowane tylko na papierze, patrz uwaga na końcu):

    idf.py set-target esp32
    idf.py build flash monitor

Sterownik W5500:
- IDF 5.3-5.5: wbudowany; `sdkconfig.defaults` go włącza (menuconfig -> Component config -> Ethernet -> SPI Ethernet -> W5500).
- IDF >= 6.0: sterownik jest osobnym komponentem `espressif/w5500`; pobiera go menedżer komponentów
  na podstawie `main/idf_component.yml` (przy pierwszym buildzie potrzebny internet).

PlatformIO: w `platformio.ini` jest `src_dir = main`, więc nic nie trzeba przenosić.

## Konfiguracja (konsola szeregowa, 115200)

Ustawienia trafiają do NVS i przetrwają restart. Przykład:

    set wifi_ssid MojaSiec
    set wifi_pass tajnehaslo
    set wifi_ip 192.168.1.50
    set wifi_mask 255.255.255.0
    set wifi_gw 192.168.1.1
    set wifi_dns 192.168.1.1
    set ntp_server pool.ntp.org
    set tz CET-1CEST,M3.5.0,M10.5.0/3
    sync

Inne komendy: `show`, `time`, `settime RRRR-MM-DD GG:MM:SS` (czas lokalny), `push`, `scope <SCPI>`,
`factory`, `reboot`. Wartości ze spacjami podawaj w cudzysłowie.

## Format komend SCPI

Zgodnie z Programming Guide (SDS800X HD) oscyloskop przyjmuje datę i czas jako liczby bez separatorów:
`:SYSTem:DATE 20191220` (RRRRMMDD) i `:SYSTem:TIME 081040` (GGMMSS). Domyślne wzorce w firmware
(`scope_date_fmt`, `scope_time_fmt`) to odpowiednio `:SYSTem:DATE %Y%m%d` i `:SYSTem:TIME %H%M%S`.
Wzorce są w formacie `strftime` i zmienisz je bez rekompilacji, np. `set scope_date_fmt "..."`.
Odczyt kontrolny: `scope :SYSTem:DATE?` i `scope :SYSTem:TIME?`; po każdej wysyłce widać go też w logu.

## Zachowanie

- Start: czas z DS3231 -> zegar systemowy -> wysyłka do oscyloskopu (ponawiana co 3 s przez 10 min,
  bo oscyloskop startuje wolniej niż ESP32).
- Jeśli `wifi_ssid` jest ustawione i `wifi_boot` = 1: równolegle próba Wi-Fi + NTP; po sukcesie czas jest
  zapisywany do DS3231 (UTC) i wysyłany do oscyloskopu ponownie, a Wi-Fi jest wyłączane.
- W terenie, przy zasilaniu z USB oscyloskopu, rozważ `set wifi_boot 0` (mniejszy pobór prądu).
- Każde podniesienie linku Ethernet (np. restart oscyloskopu przy zasilaniu pudełka z powerbanku) wywołuje
  ponowną wysyłkę czasu; przycisk BOOT nadal działa jako ręczne wymuszenie.
- `scope_resync` (minuty) włącza okresowe ponawianie wysyłki, domyślnie 0.

Uwaga: konwersja daty (`timeutil.h`) była testowana na PC; reszta - na sprzęcie przez użytkownika, w toku.
