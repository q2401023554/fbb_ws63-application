#ifndef MQTT_REPORT_H
#define MQTT_REPORT_H

#include <stdbool.h>

/**
 * @brief 创建并连接 MQTT 客户端，同时订阅指令主题
 * @return 0 成功，-1 失败（调用方可延时后重试）
 */
int mqtt_report_connect(void);

/**
 * @brief 发布一条 JSON 消息到上报主题
 * @param payload JSON 字符串
 * @return 0 成功，-1 失败
 */
int mqtt_report_publish(const char *payload);

/**
 * @brief 泵一下 MQTT 收发（内部调用 MQTTClient_yield），让订阅消息有机会进来
 */
void mqtt_report_poll(void);

/**
 * @brief 收到 QUERY 查询指令后置位，由主循环取走并立即上报一次
 * @return true = 有一次待处理的查询请求
 */
bool mqtt_report_take_query_flag(void);

/**
 * @brief 查询当前是否处于已连接状态
 * @return true = 已连接
 */
bool mqtt_report_is_connected(void);

/**
 * @brief 断线重连：销毁旧 client 后重新连接并订阅
 * @return 0 成功，-1 失败（可稍后重试）
 */
int mqtt_report_reconnect(void);

/**
 * @brief 是否存在待处理的重连请求（连接丢失回调置位）
 * @return true = 需要重连
 */
bool mqtt_report_need_reconnect(void);

#endif
