#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include "errcode.h"

#define CONFIG_WIFI_SSID "YOUR_WIFI_SSID" // 要连接的WiFi 热点账号
#define CONFIG_WIFI_PWD "YOUR_WIFI_PASSWORD"          // 要连接的WiFi 热点密码

errcode_t wifi_connect(void);
#endif
