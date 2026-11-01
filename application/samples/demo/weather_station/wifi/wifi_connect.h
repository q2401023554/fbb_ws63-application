#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include "errcode.h"

/* 要连接的 WiFi 热点账号（手机热点），改这里即可换网络。
 * 公开仓库中保留占位值，不要把真实热点名/密码提交上来。 */
#define CONFIG_WIFI_SSID "YOUR_WIFI_SSID"
/* 要连接的 WiFi 热点密码 */
#define CONFIG_WIFI_PWD  "YOUR_WIFI_PASSWORD"

errcode_t wifi_connect(void);
char *wifi_get_ip(void);

#endif
