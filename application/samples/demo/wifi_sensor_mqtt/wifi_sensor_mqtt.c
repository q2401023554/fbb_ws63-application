/**
 * 综合项目开发实践（一）主程序（移植自同学已验证的 mqtt_sensor_demo）
 *
 * 功能：
 *   1. 开发板以 STA 模式连接 WiFi 热点；
 *   2. 通过 MQTT 协议与电脑上的 MQTT 客户端（MQTTX）通信；
 *   3. 周期采集 DHT11 温湿度 + 光敏电阻光照强度，打包成 JSON 上报到电脑端。
 *
 * 数据格式（发布到 MQTT_TOPIC_PUB）：
 *   {"dev":"ws63","temp":26,"humi":58,"light":42,"volt":1386,"dark":0}
 *
 * ─── 硬件接线（开发板 Pxx 编号 == GPIO 编号）───
 *   温湿度 DHT11 模块（3 针，模块自带 4.7k 上拉，无需外接电阻）
 *       VCC  -> 3.3V
 *       DATA -> P13  (GPIO 13)      ← 普通数字脚即可
 *       GND  -> GND
 *   光敏电阻模块（3 针，只有 AO 模拟输出，必须走 ADC）
 *       VCC  -> 3.3V
 *       AO   -> P12   (ADC 通道 5)
 *       GND  -> GND
 *
 * ⚠ ADC 通道与引脚在 WS63 HAL 里是写死的绑定（不能各选各的）：
 *     CH0→P7  CH1→P8  CH2→P9  CH3→P10  CH4→P11(本板未引出)  CH5→P12
 *   光敏 AO 接 P12 -> 通道 5（当前）。
 */
#include "soc_osal.h"
#include "app_init.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>           /* uintptr_t */
#include <securec.h>          /* snprintf_s / memset_s 等安全函数 */
#include "common_def.h"
#include "MQTTClientPersistence.h"
#include "MQTTClient.h"
#include "errcode.h"
#include "wifi/wifi_connect.h"
#include "sensor/dht11.h"
#include "sensor/light_sensor.h"

#define SERVER_IP_ADDR   "192.168.1.100"   /* 运行 EMQX 的电脑 IP（手机热点下电脑当前 IP） */
#define SERVER_IP_PORT   1883              /* MQTT 默认端口 */
#define CLIENT_ID        "ws63-sensor"     /* 客户端唯一标识 */

/* 上报主题：开发板 -> 电脑（与 MQTTX 订阅主题一致） */
#define MQTT_TOPIC_PUB   "pubTopic"
/* 订阅主题：电脑 -> 开发板（接收控制指令） */
#define MQTT_TOPIC_SUB   "subTopic"

/* 上报周期。osDelay 单位为 10ms，故 5s 周期写作 osDelay(500)；
 * DHT11 单总线要求两次读取间隔 >= 1s，5s 满足要求。 */
#define REPORT_INTERVAL_TICKS 500   /* 500 x 10ms = 5s */

osThreadId_t mqtt_init_task_id;
MQTTClient client;
volatile MQTTClient_deliveryToken deliveredToken;
extern int MQTTClient_init(void);

/* 传感器读数（跨函数使用） */
static dht11_data_t g_dht = {0};
static light_data_t g_light = {0};

/* 回调函数，处理消息送达确认 */
void delivered(void *context, MQTTClient_deliveryToken dt)
{
    unused(context);
    printf("Message with token value %d delivery confirmed\r\n", dt);
    deliveredToken = dt;
}

/* 回调函数，处理接收到的消息 */
int messageArrived(void *context, char *topicname, int topiclen, MQTTClient_message *message)
{
    unused(context);
    unused(topiclen);
    printf("Message arrived on topic: %s\r\n", topicname);
    printf("Message: %.*s\n", message->payloadlen, (char *)message->payload);
    return 1; /* 表示消息已被处理 */
}

/* 回调函数，处理连接丢失 */
void connlost(void *context, char *cause)
{
    unused(context);
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
int mqtt_publish(const char *topic, char *payload)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int ret = 0;
    pubmsg.payload = payload;
    pubmsg.payloadlen = (int)strlen(payload);
    pubmsg.qos = 1;
    pubmsg.retained = 0;
    ret = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("mqtt_publish failed\r\n");
    }
    printf("mqtt_publish(), the payload is %s, the topic is %s\r\n", payload, topic);
    return ret;
}

/**
 * @brief 采集一次两个传感器的数据，并打包成 JSON 字符串
 */
static void collect_and_report(void)
{
    char json[160] = {0};

    /* 1. 读取 DHT11 温湿度；失败时用上一次的有效值 */
    dht11_data_t tmp = {0};
    if (dht11_read(&tmp) == 0) {
        g_dht = tmp;
    } else {
        printf("dht11 read fail, use last value\r\n");
    }

    /* 2. 读取光敏 AD 值 */
    light_data_t lt = {0};
    if (light_sensor_read(&lt) == 0) {
        g_light = lt;
    } else {
        printf("light sensor read fail, use last value\r\n");
    }

    /* 3. 打包 JSON：温湿度 + 光照（整数，避免 WS63 printf 不支持 %f） */
    (void)snprintf_s(json, sizeof(json), sizeof(json) - 1,
                     "{\"dev\":\"ws63\",\"temp\":%d,\"humi\":%d,"
                     "\"light\":%d,\"volt\":%d,\"dark\":%d}",
                     g_dht.temperature,
                     g_dht.humidity,
                     g_light.percent,
                     g_light.voltage_mv,
                     g_light.is_dark);

    printf("report: %s\r\n", json);
    (void)mqtt_publish(MQTT_TOPIC_PUB, json);
}

static errcode_t mqtt_connect(void)
{
    int ret;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

    /* 初始化传感器（失败不中断，仍继续联网上报，便于先看到串口日志定位问题） */
    if (dht11_init() != 0) {
        printf("WARN: dht11 init failed, temp/humi will stay 0\r\n");
    }
    if (light_sensor_init() != 0) {
        printf("WARN: light sensor init failed, light will stay 0\r\n");
    }

    /* 初始化 MQTT 客户端 */
    MQTTClient_init();

    /* 创建 MQTT 客户端 */
    ret = MQTTClient_create(&client, SERVER_IP_ADDR, CLIENT_ID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("Failed to create MQTT client, return code %d\r\n", ret);
        return ERRCODE_FAIL;
    }
    conn_opts.keepAliveInterval = 120; /* 保活时间 */
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
    osDelay(200); /* 延时200ms */

    /* 订阅控制主题（接收电脑端下发的指令） */
    mqtt_subscribe(MQTT_TOPIC_SUB);

    /* 周期采集并上报两个传感器的数据（每 5s 一次） */
    while (1) {
        collect_and_report();
        osDelay(REPORT_INTERVAL_TICKS);   /* osDelay 单位为 10ms */
    }
    return ERRCODE_SUCC;
}

void mqtt_init_task(const char *argument)
{
    unused(argument);
    wifi_connect();
    osDelay(200); /* 延时200ms，等待网络稳定 */
    mqtt_connect();
}

static void wifi_sensor_mqtt_example(void)
{
    printf("Enter wifi_sensor_mqtt_example()!");
    osThreadAttr_t options;
    options.name = "mqtt_init_task";
    options.attr_bits = 0;
    options.cb_mem = NULL;
    options.cb_size = 0;
    options.stack_mem = NULL;
    options.stack_size = 0x3000;   /* 栈放大，避免 MQTT 收发时内存不足 */
    options.priority = osPriorityNormal;
    mqtt_init_task_id = osThreadNew((osThreadFunc_t)mqtt_init_task, NULL, &options);
    if (mqtt_init_task_id != NULL) {
        printf("ID = %lu, Create mqtt_init_task_id is OK!\r\n",
               (unsigned long)(uintptr_t)mqtt_init_task_id);
    }
}

app_run(wifi_sensor_mqtt_example);
