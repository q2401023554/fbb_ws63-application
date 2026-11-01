#===============================================================================
# @brief    cmake file for wifi_sensor_mqtt sample (实验五：双传感器 + MQTT 上传)
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
#===============================================================================
set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi/wifi_connect.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sensor/dht11.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sensor/light_sensor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi_sensor_mqtt.c
)
set(HEADER_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi
    ${CMAKE_CURRENT_SOURCE_DIR}/sensor
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(SOURCES "${SOURCES}" "${SOURCES_LIST}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" "${HEADER_LIST}" PARENT_SCOPE)
