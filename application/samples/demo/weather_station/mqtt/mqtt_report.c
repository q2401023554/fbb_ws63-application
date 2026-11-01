/**
 * MQTT 上报模块（基于 SDK 内置 Paho MQTT）
 * 功能：
 *   - 连接电脑上的 EMQX Broker（MQTTX 可视化查看 = 远程 UI）；
 *   - 周期发布传感器数据 JSON；
 *   - 订阅 subTopic，收到 "QUERY" 或 {"cmd":"query"} 时置位查询标志，
 *     主循环取走标志后立即上报一次（远程主动查询）。
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "securec.h"
#include "soc_osal.h"
#include "cmsis_os2.h"
#include "MQTTClientPersistence.h"
#include "MQTTClient.h"
#include "errcode.h"
#include "app_config.h"
#include "mqtt_report.h"

extern int MQTTClient_init(void);

static MQTTClient g_client = NULL;
static volatile bool g_query_flag = false;
static volatile bool g_need_reconnect = false;  /* 掉线后置位，由主循环负责重连 */

/* 回调：消息送达确认 */
static void delivered(void *context, MQTTClient_deliveryToken dt)
{
    (void)context;
    printf("[MQTT] delivery confirmed, token=%d\r\n", dt);
}

/* 回调：收到订阅消息（远程指令） */
static int message_arrived(void *context, char *topic_name, int topic_len, MQTTClient_message *message)
{
    (void)context;
    (void)topic_len;
    printf("[MQTT] cmd arrived on %s: %.*s\r\n",
           topic_name, message->payloadlen, (char *)message->payload);

    /* 指令格式兼容两种：文本 "QUERY" / JSON {"cmd":"query"}（大小写均可） */
    char cmd[64] = {0};
    int copy_len = message->payloadlen < (int)sizeof(cmd) - 1 ? message->payloadlen : (int)sizeof(cmd) - 1;
    if (memcpy_s(cmd, sizeof(cmd), message->payload, (size_t)copy_len) == EOK) {
        cmd[copy_len] = '\0';
        /* 统一转大写后再比较，简化判断 */
        for (int i = 0; cmd[i] != '\0'; i++) {
            if ((cmd[i] >= 'a') && (cmd[i] <= 'z')) {
                cmd[i] = (char)(cmd[i] - 'a' + 'A');
            }
        }
        if ((strstr(cmd, "QUERY") != NULL) || (strstr(cmd, "\"CMD\":\"QUERY\"") != NULL)) {
            g_query_flag = true;
        }
    }
    MQTTClient_freeMessage(&message);
    return 1;
}

/* 回调：连接丢失 */
static void conn_lost(void *context, char *cause)
{
    (void)context;
    printf("[MQTT] connection lost: %s\r\n", cause != NULL ? cause : "unknown");
    g_need_reconnect = true;   /* 通知主循环：需要重新连接 */
}

int mqtt_report_connect(void)
{
    int ret;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

    MQTTClient_init();   /* 初始化 SDK 的 MQTT 底层 */

    ret = MQTTClient_create(&g_client, CONFIG_MQTT_BROKER_IP, CONFIG_MQTT_CLIENT_ID,
                            MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("[MQTT] create client fail, rc=%d\r\n", ret);
        return -1;
    }

    conn_opts.keepAliveInterval = 120;
    conn_opts.cleansession = 1;
    MQTTClient_setCallbacks(g_client, NULL, conn_lost, message_arrived, delivered);

    ret = MQTTClient_connect(g_client, &conn_opts);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("[MQTT] connect to %s fail, rc=%d\r\n", CONFIG_MQTT_BROKER_IP, ret);
        MQTTClient_destroy(&g_client);
        g_client = NULL;
        return -1;
    }
    printf("[MQTT] connected to %s:%d\r\n", CONFIG_MQTT_BROKER_IP, CONFIG_MQTT_BROKER_PORT);

    /* 订阅指令主题，支持远程 QUERY 查询 */
    ret = MQTTClient_subscribe(g_client, MQTT_TOPIC_SUB, 1);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("[MQTT] subscribe %s fail, rc=%d\r\n", MQTT_TOPIC_SUB, ret);
        /* 订阅失败也要销毁 client，避免留下半连接状态（g_client 非 NULL 但不可用）*/
        MQTTClient_disconnect(g_client, 100);
        MQTTClient_destroy(&g_client);
        g_client = NULL;
        return -1;
    }
    printf("[MQTT] subscribed %s (send QUERY to query now)\r\n", MQTT_TOPIC_SUB);
    return 0;
}

int mqtt_report_publish(const char *payload)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;

    if (g_client == NULL) {
        return -1;   /* 尚未连接，静默失败，由主循环重连 */
    }
    pubmsg.payload = (void *)payload;
    pubmsg.payloadlen = (int)strlen(payload);
    pubmsg.qos = 1;
    pubmsg.retained = 0;

    if (MQTTClient_publishMessage(g_client, MQTT_TOPIC_PUB, &pubmsg, &token) != MQTTCLIENT_SUCCESS) {
        printf("[MQTT] publish fail\r\n");
        return -1;
    }
    return 0;
}

void mqtt_report_poll(void)
{
    if (g_client != NULL) {
        MQTTClient_yield();   /* 让出控制权，处理保活与订阅消息 */
    }
}

bool mqtt_report_take_query_flag(void)
{
    bool flag = g_query_flag;
    g_query_flag = false;
    return flag;
}

bool mqtt_report_is_connected(void)
{
    if (g_client == NULL) {
        return false;
    }
    return MQTTClient_isConnected(g_client) == 1;
}

/**
 * @brief 断线重连：销毁旧 client 后重新连接 + 订阅
 * @return 0 成功，-1 失败（可稍后重试）
 *
 * 供主循环在检测到 g_need_reconnect 时调用。重连成功前先销毁旧句柄，
 * 避免新旧 client 并存造成资源泄漏。
 */
int mqtt_report_reconnect(void)
{
    if (g_client != NULL) {
        MQTTClient_disconnect(g_client, 100);
        MQTTClient_destroy(&g_client);
        g_client = NULL;
    }
    g_need_reconnect = false;
    if (mqtt_report_connect() == 0) {
        printf("[MQTT] reconnect success\r\n");
        return 0;
    }
    printf("[MQTT] reconnect fail, will retry later\r\n");
    return -1;
}

bool mqtt_report_need_reconnect(void)
{
    return g_need_reconnect;
}
