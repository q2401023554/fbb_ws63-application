#===============================================================================
# @brief    cmake file — Wi-Fi 远程灯控系统（期末项目）
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2024-2024. All rights reserved.
#===============================================================================

set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi/wifi_connect.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sensor/light_adc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mqtt/mqtt_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/actuator/pwm_led.c
    ${CMAKE_CURRENT_SOURCE_DIR}/actuator/tm1637.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)
set(HEADER_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi
    ${CMAKE_CURRENT_SOURCE_DIR}/sensor
    ${CMAKE_CURRENT_SOURCE_DIR}/mqtt
    ${CMAKE_CURRENT_SOURCE_DIR}/actuator
)

# 把局部源码列表写入 SOURCES 并提升到父级作用域，交由根目录统一构建
set(SOURCES "${SOURCES}" "${SOURCES_LIST}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" "${HEADER_LIST}" PARENT_SCOPE)
