/**
 * MQTT 通信模块
 *
 * 职责：
 *   1. 连接电脑上的 EMQX broker，订阅下行控制主题、向状态主题发布报文；
 *   2. 把收到的下行指令原样交给上层注册的回调（指令语义由 main.c 解释）；
 *   3. 提供两个轻量 JSON 取值工具，避免为固定报文引入 cJSON。
 *
 * ─── 主题约定 ───
 *   上行（设备 → 电脑）：light/status
 *     {"on":1,"bright":80,"lux":46}
 *   下行（电脑 → 设备）：light/control
 *     {"on":1}  {"on":0}  {"bright":50}
 */
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdint.h>

/* EMQX 所在电脑的 IP（换环境时必须改这里） */
#define MQTT_SERVER_IP   "172.20.10.4"
#define MQTT_SERVER_PORT 1883
#define MQTT_CLIENT_ID   "ws63_lightctl"

/* 上行 / 下行主题 */
#define MQTT_TOPIC_PUB   "light/status"
#define MQTT_TOPIC_SUB   "light/control"

/* 下行指令回调：payload 为以 '\0' 结尾的报文文本 */
typedef void (*mqtt_cmd_cb_t)(const char *payload);

/**
 * @brief 注册下行指令回调（在 connect 之前调用）
 * @param cb 回调函数
 */
void mqtt_client_set_cmd_callback(mqtt_cmd_cb_t cb);

/**
 * @brief 连接 broker 并订阅下行主题（可重复调用，用于断线重连）
 * @return 0 成功，-1 失败
 */
int mqtt_client_connect(void);

/**
 * @brief 发布一条报文
 * @param topic   主题
 * @param payload 报文文本
 * @return 0 成功，-1 失败
 */
int mqtt_client_publish_str(const char *topic, const char *payload);

/**
 * @brief 当前是否与 broker 保持连接
 * @return 1 = 已连接，0 = 未连接
 */
uint8_t mqtt_client_is_connected(void);

/**
 * @brief 从 JSON 文本中取指定 key 的值（字符串去引号，数组保留方括号）
 * @param json    报文文本
 * @param key     键名（不含引号）
 * @param out     输出缓冲
 * @param out_len 缓冲长度
 * @return 1 = 找到，0 = 未找到或参数非法
 */
int mqtt_json_find_value(const char *json, const char *key, char *out, uint32_t out_len);

/**
 * @brief 从 JSON 文本中取指定 key 的整数值
 * @param json 报文文本
 * @param key  键名（不含引号）
 * @param out  输出整数
 * @return 1 = 找到，0 = 未找到或参数非法
 */
int mqtt_json_find_int(const char *json, const char *key, int *out);

#endif
