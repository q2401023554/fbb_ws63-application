/**
 * MQTT 通信模块实现
 *
 * 基于 SDK 内置的 paho.mqtt.c（MQTTClient.h）。连接/订阅/发布的调用流程
 * 与课程示例 demo/mqtt_sensor 保持一致，额外增加：
 *   - 指令回调注册，让业务层决定指令语义；
 *   - 连接状态记录，供主循环判断是否需要重连；
 *   - 两个不依赖 cJSON 的轻量 JSON 取值函数。
 */
#include <stdio.h>
#include <string.h>
#include "common_def.h"
#include "securec.h"
#include "soc_osal.h"
#include "MQTTClient.h"
#include "MQTTClientPersistence.h"
#include "mqtt_client.h"

/* SDK 内部提供的初始化入口 */
extern int MQTTClient_init(void);

#define MQTT_KEEPALIVE_S    60
#define MQTT_PAYLOAD_MAX    192

static MQTTClient g_client = NULL;
static uint8_t g_connected = 0;
static mqtt_cmd_cb_t g_cmd_cb = NULL;

/* ---------------- 回调 ---------------- */

static void mqtt_delivered_cb(void *context, MQTTClient_deliveryToken dt)
{
    unused(context);
    printf("mqtt: message %d delivery confirmed\r\n", dt);
}

static void mqtt_connlost_cb(void *context, char *cause)
{
    unused(context);
    g_connected = 0;
    printf("mqtt: connection lost: %s\r\n", (cause != NULL) ? cause : "unknown");
}

static int mqtt_message_cb(void *context, char *topicname, int topiclen, MQTTClient_message *message)
{
    char payload[MQTT_PAYLOAD_MAX] = {0};
    int len;

    unused(context);
    unused(topiclen);

    if ((message == NULL) || (message->payload == NULL) || (message->payloadlen <= 0)) {
        return 1;
    }

    len = message->payloadlen;
    if (len > (int)sizeof(payload) - 1) {
        len = (int)sizeof(payload) - 1;
    }
    if (memcpy_s(payload, sizeof(payload), message->payload, (size_t)len) != EOK) {
        printf("mqtt: memcpy_s failed\r\n");
        return 1;
    }
    payload[len] = '\0';

    printf("mqtt: message arrived on [%s]: %s\r\n",
           (topicname != NULL) ? topicname : "?", payload);

    if (g_cmd_cb != NULL) {
        g_cmd_cb(payload);
    }
    return 1;
}

/* ---------------- 对外接口 ---------------- */

void mqtt_client_set_cmd_callback(mqtt_cmd_cb_t cb)
{
    g_cmd_cb = cb;
}

uint8_t mqtt_client_is_connected(void)
{
    return g_connected;
}

int mqtt_client_connect(void)
{
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int ret;

    if (g_connected != 0U) {
        return 0;
    }

    MQTTClient_init();

    if (g_client == NULL) {
        ret = MQTTClient_create(&g_client, MQTT_SERVER_IP, MQTT_CLIENT_ID,
                                MQTTCLIENT_PERSISTENCE_NONE, NULL);
        if (ret != MQTTCLIENT_SUCCESS) {
            printf("mqtt: MQTTClient_create failed, ret = %d (检查 SERVER_IP 是否可达)\r\n", ret);
            return -1;
        }
        MQTTClient_setCallbacks(g_client, NULL, mqtt_connlost_cb, mqtt_message_cb, mqtt_delivered_cb);
    }

    conn_opts.keepAliveInterval = MQTT_KEEPALIVE_S;
    conn_opts.cleansession = 1;

    ret = MQTTClient_connect(g_client, &conn_opts);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("mqtt: MQTTClient_connect failed, ret = %d\r\n", ret);
        return -1;
    }

    g_connected = 1U;

    /* 订阅下行控制主题 */
    ret = MQTTClient_subscribe(g_client, MQTT_TOPIC_SUB, 1);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("mqtt: subscribe [%s] failed, ret = %d\r\n", MQTT_TOPIC_SUB, ret);
    }

    printf("mqtt: connected to %s:%d, sub=[%s], pub=[%s]\r\n",
           MQTT_SERVER_IP, MQTT_SERVER_PORT, MQTT_TOPIC_SUB, MQTT_TOPIC_PUB);
    return 0;
}

int mqtt_client_publish_str(const char *topic, const char *payload)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int ret;

    if ((topic == NULL) || (payload == NULL)) {
        return -1;
    }
    if (g_connected == 0U) {
        return -1;
    }

    pubmsg.payload = (void *)payload;
    pubmsg.payloadlen = (int)strlen(payload);
    pubmsg.qos = 1;
    pubmsg.retained = 0;

    ret = MQTTClient_publishMessage(g_client, topic, &pubmsg, &token);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("mqtt: publish failed, ret = %d\r\n", ret);
        return -1;
    }
    printf("mqtt: publish [%s] %s\r\n", topic, payload);
    return 0;
}

/* ---------------- 轻量 JSON 取值 ---------------- */

/* 定位 "key" 之后、冒号之后第一个非空白字符 */
static const char *mqtt_json_locate(const char *json, const char *key)
{
    char pattern[40];
    const char *p;
    int n;

    n = snprintf_s(pattern, sizeof(pattern), sizeof(pattern) - 1, "\"%s\"", key);
    if ((n <= 0) || ((size_t)n >= sizeof(pattern))) {
        return NULL;
    }

    p = strstr(json, pattern);
    if (p == NULL) {
        return NULL;
    }
    p += n;

    while ((*p == ' ') || (*p == '\t')) {
        p++;
    }
    if (*p != ':') {
        return NULL;
    }
    p++;
    while ((*p == ' ') || (*p == '\t')) {
        p++;
    }
    return p;
}

int mqtt_json_find_value(const char *json, const char *key, char *out, uint32_t out_len)
{
    const char *p;
    uint32_t i = 0;

    if ((json == NULL) || (key == NULL) || (out == NULL) || (out_len < 2U)) {
        return 0;
    }
    out[0] = '\0';

    p = mqtt_json_locate(json, key);
    if (p == NULL) {
        return 0;
    }

    if (*p == '"') {
        /* 字符串：去掉两侧引号 */
        p++;
        while ((*p != '\0') && (*p != '"') && (i < (out_len - 1U))) {
            out[i] = *p;
            i++;
            p++;
        }
    } else if (*p == '[') {
        /* 数组：保留方括号原文，交由上层逐项解析 */
        while ((*p != '\0') && (i < (out_len - 1U))) {
            out[i] = *p;
            i++;
            if (*p == ']') {
                break;
            }
            p++;
        }
    } else {
        /* 数字 / 布尔：读到分隔符为止 */
        while ((*p != '\0') && (*p != ',') && (*p != '}') && (*p != ' ') && (i < (out_len - 1U))) {
            out[i] = *p;
            i++;
            p++;
        }
    }

    out[i] = '\0';
    return 1;
}

int mqtt_json_find_int(const char *json, const char *key, int *out)
{
    char buf[16];
    int sign = 1;
    int value = 0;
    uint32_t i = 0;

    if ((out == NULL) || (mqtt_json_find_value(json, key, buf, sizeof(buf)) == 0)) {
        return 0;
    }
    if (buf[0] == '\0') {
        return 0;
    }

    if (buf[0] == '-') {
        sign = -1;
        i = 1U;
    }
    if ((buf[i] < '0') || (buf[i] > '9')) {
        return 0;
    }
    while ((buf[i] >= '0') && (buf[i] <= '9')) {
        value = (value * 10) + (int)(buf[i] - '0');
        i++;
    }

    *out = value * sign;
    return 1;
}
