/**
 * 网络天气获取模块（HTTP 客户端 + 简易 JSON 解析）
 *
 * 实现方式（初学者友好，不依赖 SDK 高级组件）：
 *   1. netconn_gethostbyname() 完成域名解析（lwIP 自带 DNS）；
 *   2. lwIP socket 建立 TCP 连接（80 端口）；
 *   3. 手写 HTTP GET 请求报文，读取响应；
 *   4. 在 JSON 里查找 "temperature_2m" / "relative_humidity_2m" 字段并解析数值。
 *
 * 数据源：Open-Meteo（https://open-meteo.com），免费开放、无需注册 API key。
 * 如需更换城市，改下面的 WEATHER_URL_PATH 里的 latitude / longitude 即可
 * （武汉约 30.35N, 114.31E；长沙 28.23N, 112.94E；北京 39.90N, 116.41E）。
 */
#include <stdio.h>
#include <string.h>
#include <securec.h>
#include "soc_osal.h"
#include "cmsis_os2.h"
#include "lwip/sockets.h"
#include "lwip/api.h"          /* netconn_gethostbyname */
#include "lwip/ip_addr.h"
#include "net/weather_http.h"

/* ===== 天气接口配置 ===== */
#define WEATHER_HOST        "api.open-meteo.com"
#define WEATHER_PORT        80
#define WEATHER_URL_PATH    "/v1/forecast?latitude=30.35&longitude=114.31" \
                            "&current=temperature_2m,relative_humidity_2m" \
                            "&timezone=Asia%2FShanghai"

/* 接收缓冲区：该接口响应约 600 字节，取 3KB 留足余量 */
#define HTTP_BUF_SIZE  3072

/**
 * @brief 在 JSON 文本中查找指定 key 并解析其数值（支持负数与小数，四舍五入取整）
 * @return 0 成功，-1 未找到或格式不符
 *
 * 示例："temperature_2m":25.4  ->  25
 *      "relative_humidity_2m":60  ->  60
 */
static int json_parse_number(const char *json, const char *key, int32_t *out)
{
    const char *p = strstr(json, key);
    if (p == NULL) {
        return -1;
    }
    p = strchr(p + strlen(key), ':');   /* 跳到 key 后面的冒号 */
    if (p == NULL) {
        return -1;
    }
    p++;
    while (*p == ' ') {                  /* 跳过空格 */
        p++;
    }

    int32_t sign = 1;
    if (*p == '-') {
        sign = -1;
        p++;
    }
    /* 冒号后必须紧跟数字：遇到 null / "°C" 等非数值一律判解析失败，
     * 防止把单位字符串误解析成 0（真实踩过的坑：响应里 current_units 块也有同名字段）*/
    if ((*p < '0') || (*p > '9')) {
        return -1;
    }

    /* 整数部分 */
    int32_t int_part = 0;
    while (*p >= '0' && *p <= '9') {
        int_part = int_part * 10 + (*p - '0');
        p++;
    }
    /* 小数部分只取第 1 位（用于四舍五入，精度足够） */
    int32_t frac = 0;
    if (*p == '.') {
        p++;
        if ((*p >= '0') && (*p <= '9')) {
            frac = *p - '0';
        }
    }

    /* 放大 10 倍后四舍五入：25.4 -> 254 -> 25；25.6 -> 256 -> 26 */
    int32_t value10 = int_part * 10 + frac;
    *out = sign * ((value10 + 5) / 10);
    return 0;
}

int weather_http_fetch(weather_data_t *out)
{
    /* 所有变量集中声明在函数顶部：
     * SDK 开启了 -Werror=jump-misses-init，goto 不允许跳过变量初始化 */
    ip_addr_t server_ip;
    struct sockaddr_in addr;
    char *recv_buf = NULL;
    char request[256];
    int sock = -1;
    int total = 0;
    int ret_val = -1;
    int32_t temp = 0;
    int32_t humi = 0;
    const char *body = NULL;   /* "current":{ 数据块起始位置 */
    u32_t dns_count = 1;   /* 本 SDK 扩展：要解析的地址个数，必须 >= 1 */

    if (out == NULL) {
        return -1;
    }
    out->valid = false;

    /* ---------- 1. 域名解析（本 SDK 为 3 参数版本，多一个 count 入参）---------- */
    if (netconn_gethostbyname(WEATHER_HOST, &server_ip, &dns_count) != ERR_OK) {
        printf("[HTTP] DNS resolve fail: %s\r\n", WEATHER_HOST);
        return -1;
    }
    printf("[HTTP] %s -> %s\r\n", WEATHER_HOST, ip4addr_ntoa(&server_ip.u_addr.ip4));

    /* ---------- 2. 建立 TCP 连接 ---------- */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("[HTTP] create socket fail\r\n");
        return -1;
    }

    (void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(WEATHER_PORT);
    addr.sin_addr.s_addr = server_ip.u_addr.ip4.addr;

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("[HTTP] connect to weather server fail\r\n");
        goto cleanup;
    }

    /* ---------- 3. 发送 HTTP GET 请求 ---------- */
    ret_val = snprintf_s(request, sizeof(request), sizeof(request) - 1,
                         "GET %s HTTP/1.1\r\n"
                         "Host: %s\r\n"
                         "User-Agent: ws63-weather-station\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         WEATHER_URL_PATH, WEATHER_HOST);
    if (ret_val <= 0) {
        printf("[HTTP] build request fail\r\n");
        ret_val = -1;
        goto cleanup;
    }

    if (send(sock, request, (size_t)ret_val, 0) <= 0) {
        printf("[HTTP] send request fail\r\n");
        ret_val = -1;
        goto cleanup;
    }
    printf("[HTTP] request sent, waiting response...\r\n");

    /* ---------- 4. 接收响应（Connection: close，服务端发完会主动断开）---------- */
    recv_buf = (char *)osal_kmalloc(HTTP_BUF_SIZE, OSAL_GFP_ATOMIC);
    if (recv_buf == NULL) {
        printf("[HTTP] malloc recv buf fail\r\n");
        ret_val = -1;
        goto cleanup;
    }
    (void)memset_s(recv_buf, HTTP_BUF_SIZE, 0, HTTP_BUF_SIZE);

    while (total < HTTP_BUF_SIZE - 1) {
        int n = recv(sock, recv_buf + total, HTTP_BUF_SIZE - 1 - total, 0);
        if (n <= 0) {
            break;   /* 对端关闭或出错：HTTP 响应已接收完毕 */
        }
        total += n;
    }
    recv_buf[total] = '\0';
    printf("[HTTP] received %d bytes\r\n", total);

    if (total <= 0) {
        printf("[HTTP] no response data\r\n");
        ret_val = -1;
        goto cleanup;
    }

    /* ---------- 5. 解析 JSON 中的温度与湿度 ----------
     * 响应里 "current_units" 单位块也有 temperature_2m/relative_humidity_2m 字段
     * （值为 "°C"/"%" 字符串）且排在 "current" 数据块前面，
     * 必须先定位 "current":{ 数据块，再在块内找键，否则会解析到单位得到 0。 */
    body = strstr(recv_buf, "\"current\":{");
    if (body == NULL) {
        printf("[HTTP] no \"current\" block in response\r\n");
        ret_val = -1;
        goto cleanup;
    }

    if ((json_parse_number(body, "\"temperature_2m\"", &temp) != 0) ||
        (json_parse_number(body, "\"relative_humidity_2m\"", &humi) != 0)) {
        printf("[HTTP] parse weather json fail\r\n");
        ret_val = -1;
        goto cleanup;
    }

    out->temp = temp;
    out->humi = humi;
    out->valid = true;
    ret_val = 0;
    printf("[HTTP] net weather: temp=%d humi=%d\r\n", temp, humi);

cleanup:
    if (recv_buf != NULL) {
        osal_kfree(recv_buf);
    }
    if (sock >= 0) {
        lwip_close(sock);
    }
    return ret_val;
}
