#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include "errcode.h"

// 要连接的WiFi热点账号（手机热点或路由器）
#define CONFIG_WIFI_SSID "YOUR_WIFI_SSID"
// 要连接的WiFi热点密码（WPA2-PSK 时不少于 8 位）
#define CONFIG_WIFI_PWD  "YOUR_WIFI_PASSWORD"

errcode_t wifi_connect(void);
char *wifi_get_ip(void);

#endif
