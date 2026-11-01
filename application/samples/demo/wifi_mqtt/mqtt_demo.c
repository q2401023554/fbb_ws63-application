/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 *
 * Description: WIFI 网络 MQTT 通信实验。
 *              在 Hi3863/WS63 上以 STA 模式连接热点并通过 DHCP 获取 IP，
 *              随后作为 MQTT 客户端连接本地 EMQX Broker，订阅 subTopic 并
 *              周期性向 pubTopic 发布消息，实现数据收发。
 *
 * History:
 * 2026-09-26, Create file (based on WIFI 网络 MQTT 通信实验).
 */
#include "soc_osal.h"
#include "app_init.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common_def.h"
#include "MQTTClientPersistence.h"
#include "MQTTClient.h"
#include "errcode.h"
#include "wifi/wifi_connect.h"

osThreadId_t mqtt_init_task_id; /* mqtt订阅数据任务 */

#define SERVER_IP_ADDR "10.121.173.189" /* TODO: 改为运行 EMQX 服务器(PC)的IP地址 */
#define SERVER_IP_PORT 1883
#define CLIENT_ID "ws63"

#define MQTT_TOPIC_SUB "subTopic"
#define MQTT_TOPIC_PUB "pubTopic"

char *g_msg = "hello!";
MQTTClient client;
volatile MQTTClient_deliveryToken deliveredToken;
extern int MQTTClient_init(void);

/* 回调函数，处理消息发布送达确认 */
void delivered(void *context, MQTTClient_deliveryToken dt)
{
    (void)context;
    printf("Message with token value %d delivery confirmed\r\n", dt);
    deliveredToken = dt;
}

/* 回调函数，处理接收到的订阅消息 */
int messageArrived(void *context, char *topicname, int topiclen, MQTTClient_message *message)
{
    (void)context;
    (void)topiclen;
    printf("Message arrived on topic: %s\r\n", topicname);
    printf("Message: %.*s\n", message->payloadlen, (char *)message->payload);
    return 1; /* 表示消息已被处理 */
}

/* 回调函数，处理连接丢失 */
void connlost(void *context, char *cause)
{
    (void)context;
    printf("Connection lost: %s\r\n", cause);
}

/* 消息订阅 */
int mqtt_subscribe(const char *topic)
{
    printf("subscribe start\r\n");
    MQTTClient_subscribe(client, topic, 1);
    return 0;
}

/* 消息发布 */
int mqtt_publish(const char *topic, char *msg)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int ret = 0;
    pubmsg.payload = msg;
    pubmsg.payloadlen = (int)strlen(msg);
    pubmsg.qos = 1;
    pubmsg.retained = 0;
    ret = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("mqtt_publish failed\r\n");
    }
    printf("mqtt_publish(), the payload is %s, the topic is %s\r\n", msg, topic);
    return ret;
}

/* MQTT客户端初始化、连接服务器、订阅主题并循环发布 */
static errcode_t mqtt_connect(void)
{
    int ret;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

    /* 初始化MQTT客户端 */
    MQTTClient_init();
    /* 创建MQTT客户端 */
    ret = MQTTClient_create(&client, SERVER_IP_ADDR, CLIENT_ID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("Failed to create MQTT client, return code %d\r\n", ret);
        return ERRCODE_FAIL;
    }
    conn_opts.keepAliveInterval = 120; /* 120: 保活时间120秒 */
    conn_opts.cleansession = 1;
    /* 绑定回调函数 */
    MQTTClient_setCallbacks(client, NULL, connlost, messageArrived, delivered);

    /* 尝试连接 */
    if ((ret = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", ret);
        MQTTClient_destroy(&client); /* 连接失败时销毁客户端 */
        return ERRCODE_FAIL;
    }
    printf("Connected to MQTT broker!\n");
    osDelay(200); /* 200: 延时2s */
    /* 订阅MQTT主题 */
    mqtt_subscribe(MQTT_TOPIC_SUB);
    while (1) {
        osDelay(100); /* 100: 延时1s */
        mqtt_publish(MQTT_TOPIC_PUB, g_msg);
    }
    return ERRCODE_SUCC;
}

/* 任务入口：先WiFi联网，再连接MQTT */
void mqtt_init_task(const char *argument)
{
    (void)argument;
    wifi_connect();
    osDelay(200); /* 200: 延时2s，等待网络稳定 */
    mqtt_connect();
}

static void network_wifi_mqtt_example(void)
{
    printf("Enter network_wifi_mqtt_example()!");
    osThreadAttr_t options;
    options.name = "mqtt_init_task";
    options.attr_bits = 0;
    options.cb_mem = NULL;
    options.cb_size = 0;
    options.stack_mem = NULL;
    options.stack_size = 0x3000;
    options.priority = osPriorityNormal;
    mqtt_init_task_id = osThreadNew((osThreadFunc_t)mqtt_init_task, NULL, &options);
    if (mqtt_init_task_id != NULL) {
        printf("ID = %d, Create mqtt_init_task_id is OK!\r\n", mqtt_init_task_id);
    }
}

/* Run the sample. */
app_run(network_wifi_mqtt_example);
