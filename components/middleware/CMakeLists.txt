idf_component_register(
    SRCS 
    "src/protocol_engine.c"
    "src/modbus_slave.c"
    "src/register_map.c"
    "src/utils.c"
    "src/utils_expr.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp json nvs_flash
)