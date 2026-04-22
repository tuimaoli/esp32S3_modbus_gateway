#pragma once
#include "bsp_serial_port.h"
#ifdef __cplusplus
extern "C" {
#endif
bsp_serial_port_t* bsp_native_uart_create(int logical_port_id, int uart_num, int tx_io, int rx_io, int rts_io, int baud_rate);
#ifdef __cplusplus
}
#endif