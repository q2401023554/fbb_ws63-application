#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include "errcode.h"

/* 要连接的 WiFi 热点账号（手机热点） */
#define CONFIG_WIFI_SSID "YOUR_WIFI_SSID"
/* 要连接的 WiFi 热点密码 */
#define CONFIG_WIFI_PWD  "YOUR_WIFI_PASSWORD"

errcode_t wifi_connect(void);
char *wifi_get_ip(void);

#endif
