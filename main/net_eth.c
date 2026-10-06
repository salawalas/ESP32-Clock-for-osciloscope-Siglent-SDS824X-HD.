#include "net_eth.h"
#include "app.h"
#include "cfg.h"
#include "pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
/* IDF >= 6.0: sterownik W5500 to zarzadzany komponent espressif/w5500 (patrz main/idf_component.yml) */
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#define W5500_CFG_INT_GPIO(c)   ((c).base.int_gpio_num)
#else
#define W5500_CFG_INT_GPIO(c)   ((c).int_gpio_num)
#endif

static const char *TAG = "eth";
static volatile bool s_link;

static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        s_link = true;
        ESP_LOGI(TAG, "link UP");
        /* Po kazdym podniesieniu linku (np. restart oscyloskopu) odswiez czas w oscyloskopie.
           scope_task ponawia proby, wiec nie szkodzi, ze oscyloskop jeszcze sie uruchamia. */
        if (g_evt) xEventGroupSetBits(g_evt, EVT_PUSH_SCOPE);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        s_link = false;
        ESP_LOGW(TAG, "link DOWN");
        break;
    default:
        break;
    }
}

bool eth_link_up(void) { return s_link; }

esp_err_t eth_start(void)
{
    uint32_t ip, mask;
    if (!ip4_parse(cfg_get("eth_ip"), &ip) || !ip4_parse(cfg_get("eth_mask"), &mask)) {
        ESP_LOGE(TAG, "zly eth_ip / eth_mask");
        return ESP_ERR_INVALID_ARG;
    }

    /* Usluga ISR dla pinu INT z W5500; blad "juz zainstalowana" ignorujemy */
    gpio_install_isr_service(0);

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_ETH_MISO,
        .mosi_io_num = PIN_ETH_MOSI,
        .sclk_io_num = PIN_ETH_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    static spi_device_interface_config_t devcfg;
    devcfg = (spi_device_interface_config_t){
        .command_bits = 16,                 /* ramka SPI W5500: 16 bit adres, 8 bit sterowanie */
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = ETH_SPI_MHZ * 1000 * 1000,
        .spics_io_num = PIN_ETH_CS,
        .queue_size = 20,
    };

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    /* IDF >= 5.3: sterownik sam dodaje urzadzenie SPI */
    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &devcfg);
#else
    /* IDF 5.2: urzadzenie SPI dodajemy sami */
    spi_device_handle_t spi = NULL;
    ESP_RETURN_ON_ERROR(spi_bus_add_device(ETH_SPI_HOST, &devcfg, &spi), TAG, "spi dev");
    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(spi);
#endif
    W5500_CFG_INT_GPIO(w5500_cfg) = PIN_ETH_INT;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = 1;
    phy_cfg.reset_gpio_num = PIN_ETH_RST;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);
    if (!mac || !phy) {
        ESP_LOGE(TAG, "nie mozna utworzyc MAC/PHY W5500 (IDF < 6: CONFIG_ETH_SPI_ETHERNET_W5500; IDF >= 6: komponent espressif/w5500)");
        return ESP_FAIL;
    }

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth = NULL;
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_cfg, &eth), TAG,
                        "install (sprawdz okablowanie SPI/zasilanie W5500)");

    /* W5500 nie ma fabrycznego MAC - bierzemy ten wyliczony przez ESP32 */
    uint8_t mac_addr[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac_addr, ESP_MAC_ETH), TAG, "read mac");
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(eth, ETH_CMD_S_MAC_ADDR, mac_addr), TAG, "set mac");

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&netif_cfg);
    ESP_RETURN_ON_FALSE(netif, ESP_FAIL, TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_netif_attach(netif, esp_eth_new_netif_glue(eth)), TAG, "attach");

    /* Staly IP, bez DHCP i bez bramy (lacze punkt-punkt do oscyloskopu) */
    esp_netif_dhcpc_stop(netif);
    esp_netif_ip_info_t info = { 0 };
    info.ip.addr = ip;
    info.netmask.addr = mask;
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(netif, &info), TAG, "set ip");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, NULL),
                        TAG, "handler");
    ESP_RETURN_ON_ERROR(esp_eth_start(eth), TAG, "start");
    ESP_LOGI(TAG, "W5500 wystartowal, IP %s", cfg_get("eth_ip"));
    return ESP_OK;
}
