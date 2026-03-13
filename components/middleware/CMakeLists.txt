idf_component_register(
    SRCS 
    "src/protocol_engine.c"
    "src/modbus_slave.c"
    #"src/sensor_parsers.c"
    "src/register_map.c"
    "src/modbus_template.c"
    "src/utils.c"
    "src/utils_expr.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp json
)