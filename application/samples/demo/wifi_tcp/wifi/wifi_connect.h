#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include "errcode.h"

#define CONFIG_WIFI_SSID "YOUR_WIFI_SSID"      // 要连接的WiFi 热点账号
#define CONFIG_WIFI_PWD "YOUR_WIFI_PASSWORD"        // 要连接的WiFi 热点密码
#define CONFIG_SERVER_IP "10.135.18.236" // 要连接的服务器IP
#define CONFIG_SERVER_PORT 6789            // 要连接的服务器端口

errcode_t wifi_connect(void);
#endif
