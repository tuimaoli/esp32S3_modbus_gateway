/**
 * @file bsp_w5100s.c
 * @brief BSP层：W5100S 驱动实现 (带双核 SMP 自旋锁)
 */
#include "bsp_w5100s.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE wiz_spinlock = portMUX_INITIALIZER_UNLOCKED;
static void wiz_cris_enter(void) { 
    portENTER_CRITICAL(&wiz_spinlock); 
}

static void wiz_cris_exit(void) { 
    portEXIT_CRITICAL(&wiz_spinlock); 
}

#ifdef MR
#undef MR
#endif
#include "Ethernet/wizchip_conf.h"

static spi_device_handle_t spi_handle = NULL;
static int g_cs_io = -1;

static void wiz_cs_select(void)   { 
    gpio_set_level(g_cs_io, 0); 
}

static void wiz_cs_deselect(void) { 
    gpio_set_level(g_cs_io, 1); 
}

static uint8_t wiz_spi_read_byte(void) {
    spi_transaction_t t = { .length = 8, .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA, .tx_data = {0xFF} };
    spi_device_polling_transmit(spi_handle, &t); return t.rx_data[0];
}

static void wiz_spi_write_byte(uint8_t wb) {
    spi_transaction_t t = { .length = 8, .flags = SPI_TRANS_USE_TXDATA, .tx_data = {wb} };
    spi_device_polling_transmit(spi_handle, &t);
}

esp_err_t bsp_w5100s_init(const bsp_w5100s_config_t *config) {
    g_cs_io = config->cs_io;
    gpio_reset_pin(g_cs_io);
    gpio_set_direction(g_cs_io, GPIO_MODE_OUTPUT);
    gpio_set_level(g_cs_io, 1);

    spi_bus_config_t buscfg = {
        .miso_io_num = config->miso_io,
        .mosi_io_num = config->mosi_io,
        .sclk_io_num = config->sclk_io,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1024
    };
    spi_bus_initialize(config->host_id, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = config->clock_speed_mhz * 1000 * 1000,
        .mode = 0, 
        .spics_io_num = -1, 
        .queue_size = 7
    };

    ESP_ERROR_CHECK(spi_bus_add_device(config->host_id, &devcfg, &spi_handle));

    if (config->rst_io >= 0) {
        gpio_reset_pin(config->rst_io);
        gpio_set_direction(config->rst_io, GPIO_MODE_OUTPUT);
        gpio_set_level(config->rst_io, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(config->rst_io, 1);
        vTaskDelay(pdMS_TO_TICKS(150)); 
    }

    reg_wizchip_cs_cbfunc(wiz_cs_select, wiz_cs_deselect);
    reg_wizchip_spi_cbfunc(wiz_spi_read_byte, wiz_spi_write_byte);
    reg_wizchip_cris_cbfunc(wiz_cris_enter, wiz_cris_exit);

    uint8_t memsize[2][4] = {{2, 2, 2, 2}, {2, 2, 2, 2}};
    ctlwizchip(CW_INIT_WIZCHIP, (void*)memsize);
    
    wiz_NetInfo netInfo = {
        .mac = {0x00, 0x08, 0xdc, 0x11, 0x22, 0x33},
        .ip = {172, 16, 10, 187},
        .sn = {255, 255, 255, 0},
        .gw = {172, 16, 10, 187},
        .dns = {8, 8, 8, 8},
        .dhcp = NETINFO_STATIC
    };
    wizchip_setnetinfo(&netInfo);
    return ESP_OK;
}