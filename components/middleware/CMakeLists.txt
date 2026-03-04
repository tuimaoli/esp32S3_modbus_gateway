idf_component_register(
    SRCS 
    "src/modbus_master.c"
    "src/modbus_slave.c"
    "src/sensor_parsers.c"
    "src/register_map.c"
    "src/modbus_template.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp
)