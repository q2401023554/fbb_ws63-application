#===============================================================================
# @brief    桌面智能气象站 cmake（南向设备开发考核项目）
# 模块划分：主程序 + 7 个驱动/功能模块（.c/.h 分离）
#===============================================================================
set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/weather_station.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sensor/dht11.c
    ${CMAKE_CURRENT_SOURCE_DIR}/display/segment_display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/status_led/status_led.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi/wifi_connect.c
    ${CMAKE_CURRENT_SOURCE_DIR}/net/weather_http.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mqtt/mqtt_report.c
    ${CMAKE_CURRENT_SOURCE_DIR}/uart/uart_cmd.c
)
set(HEADER_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/sensor
    ${CMAKE_CURRENT_SOURCE_DIR}/display
    ${CMAKE_CURRENT_SOURCE_DIR}/status_led
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi
    ${CMAKE_CURRENT_SOURCE_DIR}/net
    ${CMAKE_CURRENT_SOURCE_DIR}/mqtt
    ${CMAKE_CURRENT_SOURCE_DIR}/uart
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(SOURCES "${SOURCES}" "${SOURCES_LIST}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" "${HEADER_LIST}" PARENT_SCOPE)
