#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
#===============================================================================
if(DEFINED CONFIG_SAMPLE_SUPPORT_HELLO)
    add_subdirectory_if_exist(hello)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_LED)
    add_subdirectory_if_exist(LED)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_LED2)
    add_subdirectory_if_exist(LED2)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_UART)
    add_subdirectory_if_exist(UART)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_WIFI_AP)
    add_subdirectory_if_exist(wifi_ap)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_WIFI_STA)
    add_subdirectory_if_exist(wifi_sta)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_UDP)
    add_subdirectory_if_exist(UDP)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_WIFI_TCP)
    add_subdirectory_if_exist(wifi_tcp)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_WIFI_MQTT)
    add_subdirectory_if_exist(wifi_mqtt)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_WIFI_SENSOR_MQTT)
    add_subdirectory_if_exist(wifi_sensor_mqtt)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_WEATHER_STATION)
    add_subdirectory_if_exist(weather_station)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_WIFI_CTRL_MQTT)
    add_subdirectory_if_exist(wifi_ctrl_mqtt)
endif()

set(SOURCES "${SOURCES}" PARENT_SCOPE)
# 子样品的 PUBLIC_HEADER（头文件搜索路径）必须一并向上转发，
# 否则 build_component() 拿不到 -I 路径，跨目录包含 app_config.h 会报
# "fatal error: app_config.h: No such file or directory"。
set(PUBLIC_HEADER "${PUBLIC_HEADER}" PARENT_SCOPE)
