#ifndef WEATHER_HTTP_H
#define WEATHER_HTTP_H

#include <stdint.h>
#include <stdbool.h>

/**
 * 网络天气数据（经 HTTP + JSON 解析后得到）
 */
typedef struct {
    int32_t temp;    /* 网络气温，四舍五入取整，单位 ℃ */
    int32_t humi;    /* 网络相对湿度，取整，单位 % */
    bool valid;      /* 本次结果是否有效 */
} weather_data_t;

/**
 * @brief 通过 HTTP 获取一次网络天气（阻塞式，约 1~3s）
 * @param out 输出结果
 * @return 0 成功，-1 失败（DNS / 连接 / 解析任一环节出错）
 *
 * 数据源：Open-Meteo 开放天气接口（无需 API key，HTTP 明文 80 端口）
 * 城市坐标在 weather_http.c 顶部宏修改（默认武汉，可改为自己学校坐标）。
 */
int weather_http_fetch(weather_data_t *out);

#endif
