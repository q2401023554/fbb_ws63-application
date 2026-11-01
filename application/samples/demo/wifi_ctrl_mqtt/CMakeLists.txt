#===============================================================================
# @brief    cmake file for wifi_ctrl_mqtt sample (实验六：MQTT 下行控制 LED + 数码管)
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
#===============================================================================
set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi/wifi_connect.c
    ${CMAKE_CURRENT_SOURCE_DIR}/segment_display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi_ctrl_mqtt.c
)
set(HEADER_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(SOURCES "${SOURCES}" "${SOURCES_LIST}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" "${HEADER_LIST}" PARENT_SCOPE)
