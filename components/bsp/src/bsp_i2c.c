#include "bsp_i2c.h"
#include "driver/i2c.h"

esp_err_t bsp_i2c_init(const bsp_i2c_config_t *conf) {
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = conf->sda_io,
        .scl_io_num = conf->scl_io,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = conf->clk_speed,
    };
    i2c_param_config(conf->port_num, &i2c_conf);
    return i2c_driver_install(conf->port_num, i2c_conf.mode, 0, 0, 0);
}

esp_err_t bsp_i2c_write(int port, uint8_t addr, const uint8_t *data, size_t len) {
    return i2c_master_write_to_device(port, addr, data, len, pdMS_TO_TICKS(100));
}

esp_err_t bsp_i2c_read(int port, uint8_t addr, uint8_t *buffer, size_t len) {
    return i2c_master_read_from_device(port, addr, buffer, len, pdMS_TO_TICKS(100));
}