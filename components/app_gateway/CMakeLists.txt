idf_component_register(
     SRCS "src/gateway.c"
          "src/io_manager.c"
          "src/app_tcp_server.c"
          "src/config_manager.c"
          "src/app_webserver.c"
     INCLUDE_DIRS "include"
     REQUIRES middleware bsp json esp_http_server
)