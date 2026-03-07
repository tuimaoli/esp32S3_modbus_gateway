idf_component_register(SRCS "src/bsp_uart.c"
                            "src/bsp_i2c.c"
                            "src/bsp_gpio.c"
                            "src/bsp_w5100s.c"
                            "src/Ethernet/W5100/w5100.c"   
                            "src/Ethernet/wizchip_conf.c"
                            "src/Ethernet/socket.c"
                            "src/bsp_wifi.c"
                            "src/bsp_fs.c"
                            "src/transport_wiznet.c"
                       INCLUDE_DIRS "include" "src" "src/Ethernet" "src/Ethernet/W5100" 
                       REQUIRES driver esp_wifi esp_netif esp_event littlefs esp_http_client)

# 强制定义 WIZnet 库需要的芯片宏，消除头文件引用错误
target_compile_definitions(${COMPONENT_LIB} PUBLIC _WIZCHIP_=5100)