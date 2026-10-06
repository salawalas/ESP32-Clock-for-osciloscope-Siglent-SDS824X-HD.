#include "rtc_ds3231.h"
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "timeutil.h"

#define DS_ADDR     0x68
#define REG_TIME    0x00
#define REG_CTRL    0x0E
#define REG_STATUS  0x0F
#define XFER_MS     100

static const char *TAG = "ds3231";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, XFER_MS);
}

static esp_err_t wr(uint8_t reg, const uint8_t *data, size_t n)
{
    uint8_t tmp[8];
    if (n > sizeof(tmp) - 1) return ESP_ERR_INVALID_SIZE;
    tmp[0] = reg;
    memcpy(&tmp[1], data, n);
    return i2c_master_transmit(s_dev, tmp, n + 1, XFER_MS);
}

esp_err_t ds3231_init(int sda, int scl)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   /* moduly DS3231 zwykle maja wlasne pull-upy */
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), TAG, "i2c dev");

    /* EOSC musi byc 0, zeby zegar chodzil na baterii */
    uint8_t ctrl;
    ESP_RETURN_ON_ERROR(rd(REG_CTRL, &ctrl, 1), TAG, "DS3231 nie odpowiada (adres 0x68)");
    if (ctrl & 0x80) {
        ctrl &= (uint8_t)~0x80;
        ESP_RETURN_ON_ERROR(wr(REG_CTRL, &ctrl, 1), TAG, "zapis CTRL");
    }
    return ESP_OK;
}

esp_err_t ds3231_get_utc(time_t *utc, bool *osf)
{
    uint8_t r[7], st;
    ESP_RETURN_ON_ERROR(rd(REG_TIME, r, 7), TAG, "odczyt czasu");
    ESP_RETURN_ON_ERROR(rd(REG_STATUS, &st, 1), TAG, "odczyt statusu");

    int sec = tu_bcd2bin(r[0] & 0x7F);
    int min = tu_bcd2bin(r[1] & 0x7F);
    int hour;
    if (r[2] & 0x40) {                               /* tryb 12h (nie ustawiamy go, ale obsluzmy) */
        hour = tu_bcd2bin(r[2] & 0x1F);
        bool pm = r[2] & 0x20;
        if (pm) { if (hour < 12) hour += 12; }
        else if (hour == 12) hour = 0;
    } else {
        hour = tu_bcd2bin(r[2] & 0x3F);
    }
    int mday = tu_bcd2bin(r[4] & 0x3F);
    int mon  = tu_bcd2bin(r[5] & 0x1F);
    int year = 2000 + tu_bcd2bin(r[6]);
    if (r[5] & 0x80) year += 100;

    if (mon < 1 || mon > 12 || mday < 1 || mday > 31 || hour > 23 || min > 59 || sec > 59)
        return ESP_ERR_INVALID_RESPONSE;

    *utc = tu_epoch(year, mon, mday, hour, min, sec);
    *osf = (st & 0x80) != 0;
    return ESP_OK;
}

esp_err_t ds3231_set_utc(time_t utc)
{
    struct tm t;
    gmtime_r(&utc, &t);
    if (t.tm_year < 100 || t.tm_year > 199) return ESP_ERR_INVALID_ARG;   /* 2000..2099 */

    uint8_t r[7] = {
        tu_bin2bcd((uint8_t)t.tm_sec),
        tu_bin2bcd((uint8_t)t.tm_min),
        tu_bin2bcd((uint8_t)t.tm_hour),              /* bit 6 = 0 -> 24h */
        tu_bin2bcd((uint8_t)(t.tm_wday + 1)),
        tu_bin2bcd((uint8_t)t.tm_mday),
        tu_bin2bcd((uint8_t)(t.tm_mon + 1)),
        tu_bin2bcd((uint8_t)(t.tm_year - 100)),
    };
    ESP_RETURN_ON_ERROR(wr(REG_TIME, r, 7), TAG, "zapis czasu");

    uint8_t st;
    ESP_RETURN_ON_ERROR(rd(REG_STATUS, &st, 1), TAG, "odczyt statusu");
    st &= (uint8_t)~0x80;                            /* kasuj OSF */
    ESP_RETURN_ON_ERROR(wr(REG_STATUS, &st, 1), TAG, "zapis statusu");
    return ESP_OK;
}
