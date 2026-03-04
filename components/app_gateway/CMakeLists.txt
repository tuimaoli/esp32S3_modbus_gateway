idf_component_register(SRCS "src/gateway.c"
                            "src/io_manager.c"
                            "src/app_tcp_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bsp middleware)