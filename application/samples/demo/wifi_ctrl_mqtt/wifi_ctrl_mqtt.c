/**
 * 综合项目开发实践（二）——WIFI 网络 MQTT 通信实验（下行控制）
 *
 * 功能：
 *   1. 开发板以 STA 模式连接 WiFi 热点；
 *   2. 通过 MQTT 协议连接电脑上的 EMQX Broker；
 *   3. 订阅 subTopic，接收 MQTT 客户端（MQTTX）下发的控制指令；
 *   4. 解析指令并控制两个终端器件：
 *        - 外接 LED 模块（GPIO10）：点亮 / 熄灭
 *        - TM1637 数码管（SDA=GPIO13, SCL=GPIO14）：显示数字 / 清屏
 *   5. 执行后向 pubTopic 发布回执（ACK）与设备状态，MQTTX 订阅即可查看。
 * ─── 指令格式（MQTTX 在 subTopic 下发，大小写均可）───
 *   文本格式：
 *     LED_ON          -> LED 点亮
 *     LED_OFF         -> LED 熄灭
 *     SEG 1234        -> 数码管显示 1234（0~9999）
 *     SEG CLEAR       -> 数码管清屏
 *     LED?            -> 查询并回发当前状态
 *   JSON 格式：
 *     {"cmd":"led","state":1}   -> LED 点亮（state:0 熄灭）
 *     {"cmd":"seg","num":1234}  -> 数码管显示 1234
 *     {"cmd":"seg","num":-1}    -> 数码管清屏
 *     {"cmd":"query"}           -> 查询并回发当前状态
 *
 * ─── 硬件接线（引脚宏在 app_config.h，改线只改那里）───
 *   外接 LED 模块：IN -> GPIO10，VCC -> 3.3V/5V，GND -> GND
 *   板载数码管插槽：SDA=GPIO13，SCL=GPIO14，selector GPIO2=0 / GPIO9=1
 */
#include "soc_osal.h"
#include "app_init.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <securec.h>          /* snprintf_s / memcpy_s 等安全函数 */
#include "common_def.h"
#include "MQTTClientPersistence.h"
#include "MQTTClient.h"
#include "errcode.h"
#include "pinctrl.h"
#include "gpio.h"
#include "wifi/wifi_connect.h"
#include "app_config.h"
#include "segment_display.h"

/* ===== 通信参数 ===== */
#define SERVER_IP_ADDR   "192.168.1.100"   /* TODO: 改为运行 EMQX 的电脑 IP（手机热点下电脑当前 IP） */
#define SERVER_IP_PORT   1883              /* MQTT 默认端口 */
#define CLIENT_ID        "ws63-ctrl"       /* 客户端唯一标识 */

/* 发布主题：开发板 -> 电脑（MQTTX 订阅 pubTopic 查看回执/状态） */
#define MQTT_TOPIC_PUB   "pubTopic"
/* 订阅主题：电脑 -> 开发板（MQTTX 在此主题下发控制指令） */
#define MQTT_TOPIC_SUB   "subTopic"

/* 周期状态上报间隔：osDelay 单位为 10ms，500 x 10ms = 5s */
#define STATUS_INTERVAL_TICKS 500

osThreadId_t mqtt_init_task_id;
MQTTClient client;
volatile MQTTClient_deliveryToken deliveredToken;
extern int MQTTClient_init(void);

/* 当前执行器状态（跨函数使用，回执/状态上报要用） */
static int g_led_state = 0;        /* 0=熄灭 1=点亮 */
static int g_seg_display = 0;      /* 当前数码管显示值，-1 表示清屏 */

/* ============ LED（外接模块，GPIO10）============ */
static void led_ctrl_init(void)
{
    errcode_t ret;

    ret = uapi_pin_set_mode(LED_CTRL_PIN, HAL_PIO_FUNC_GPIO);
    if (ret != ERRCODE_SUCC) {
        printf("[INIT] LED pin mode FAIL, ret=%d\r\n", ret);
    }
    ret = uapi_gpio_set_dir(LED_CTRL_PIN, GPIO_DIRECTION_OUTPUT);
    if (ret != ERRCODE_SUCC) {
        printf("[INIT] LED pin dir FAIL, ret=%d\r\n", ret);
    }
    /* 初始状态：熄灭 */
    uapi_gpio_set_val(LED_CTRL_PIN, GPIO_LEVEL_LOW);
    g_led_state = 0;
    printf("[INIT] LED ctrl pin GPIO_%d ready, default OFF\r\n", LED_CTRL_PIN);
}

static void led_ctrl_set(int on)
{
    if (on != 0) {
        uapi_gpio_set_val(LED_CTRL_PIN, GPIO_LEVEL_HIGH);
    } else {
        uapi_gpio_set_val(LED_CTRL_PIN, GPIO_LEVEL_LOW);
    }
    g_led_state = (on != 0) ? 1 : 0;
    printf("[LED] %s\r\n", g_led_state ? "ON" : "OFF");
}

/* ============ MQTT 基础回调 ============ */
/* 回调函数，处理消息送达确认 */
void delivered(void *context, MQTTClient_deliveryToken dt)
{
    (void)context;
    printf("Message with token value %d delivery confirmed\r\n", dt);
    deliveredToken = dt;
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
    printf("mqtt_publish(), payload=%s, topic=%s\r\n", payload, topic);
    return ret;
}

/* ============ 指令解析与执行 ============ */

/* 在字符串 s 中查找 key 后第一个 ':' 后面的整数。
 * 例：{"cmd":"seg","num":1234} 中 key="num" -> 1234；num:-1 -> -1。
 * 返回 0 表示解析成功。 */
static int extract_json_int(const char *s, const char *key, int *val)
{
    const char *p = strstr(s, key);
    const char *colon;

    if (p == NULL) {
        return -1;
    }
    colon = strchr(p, ':');
    if (colon == NULL) {
        return -1;
    }
    colon++;
    while (*colon == ' ' || *colon == '\t') {
        colon++;
    }
    /* 支持负数（-1 表示清屏） */
    if (*colon == '-') {
        colon++;
        if (*colon < '0' || *colon > '9') {
            return -1;
        }
        int n = 0;
        while (*colon >= '0' && *colon <= '9') {
            n = n * 10 + (*colon - '0');
            colon++;
        }
        *val = -n;
        return 0;
    }
    if (*colon < '0' || *colon > '9') {
        return -1;
    }
    int n = 0;
    while (*colon >= '0' && *colon <= '9') {
        n = n * 10 + (*colon - '0');
        colon++;
    }
    *val = n;
    return 0;
}

/* 手写提取字符串 s 从 p 开始的连续数字（避免依赖 sscanf/%d） */
static int parse_decimal(const char *p)
{
    int n = 0;
    while (*p >= '0' && *p <= '9') {
        n = n * 10 + (*p - '0');
        p++;
    }
    return n;
}

/* 大小写不敏感的子串匹配（用于文本指令） */
static const char *strcasestr_custom(const char *haystack, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0) {
        return haystack;
    }
    for (; *haystack != '\0'; haystack++) {
        size_t i = 0;
        while (i < nl) {
            char h = haystack[i];
            char n = needle[i];
            if (h >= 'a' && h <= 'z') {
                h = (char)(h - 'a' + 'A');
            }
            if (n >= 'a' && n <= 'z') {
                n = (char)(n - 'a' + 'A');
            }
            if (h != n) {
                break;
            }
            i++;
        }
        if (i == nl) {
            return haystack;
        }
    }
    return NULL;
}

/* 发布执行回执（ACK），MQTTX 订阅 pubTopic 可见 */
static void publish_ack(const char *action)
{
    char json[160] = {0};

    (void)snprintf_s(json, sizeof(json), sizeof(json) - 1,
                     "{\"dev\":\"ws63\",\"ack\":\"%s\",\"led\":%d,"
                     "\"seg\":%d,\"ip\":\"%s\"}",
                     action, g_led_state, g_seg_display, wifi_get_ip());
    printf("ack: %s\r\n", json);
    (void)mqtt_publish(MQTT_TOPIC_PUB, json);
}

/* 核心：解析 MQTT 下发的指令字符串并控制执行器 */
static void parse_and_execute(const char *s)
{
    int led_set = -1;      /* -1=不改变  0=熄灭  1=点亮 */
    int seg_set = -2;      /* -2=不改变  -1=清屏  0~9999=显示 */
    int do_query = 0;
    int val = 0;
    const char *p = NULL;

    /* ── 1. JSON 格式（优先） ── */
    if (extract_json_int(s, "state", &val) == 0) {
        led_set = (val != 0) ? 1 : 0;    /* {"cmd":"led","state":1|0} */
    }
    if (extract_json_int(s, "num", &val) == 0) {
        seg_set = val;                    /* {"cmd":"seg","num":1234|-1} */
    }

    /* ── 2. 文本格式：LED ── */
    if (strcasestr_custom(s, "LED_ON") != NULL) {
        led_set = 1;
    } else if (strcasestr_custom(s, "LED_OFF") != NULL) {
        led_set = 0;
    }

    /* ── 3. 文本格式：数码管 "SEG 1234" / "SEG CLEAR" / "SEG_OFF" ── */
    p = strcasestr_custom(s, "SEG");
    if (p != NULL && strcasestr_custom(s, "LED_") == NULL) {
        /* 跳过 "SEG" 后的分隔符 */
        p += 3;
        while (*p == ' ' || *p == '\t' || *p == '=' || *p == ':') {
            p++;
        }
        if (*p >= '0' && *p <= '9') {
            seg_set = parse_decimal(p);
        } else if (strcasestr_custom(p, "CLEAR") != NULL ||
                   strcasestr_custom(p, "OFF") != NULL) {
            seg_set = -1;
        }
    }

    /* ── 4. 查询 ── */
    if (strcasestr_custom(s, "LED?") != NULL ||
        strcasestr_custom(s, "QUERY") != NULL) {
        do_query = 1;
    }

    /* ── 执行 ── */
    if (led_set >= 0) {
        led_ctrl_set(led_set);
    }
    if (seg_set == -1) {
        segment_display_clear();
        g_seg_display = -1;
        printf("[SEG] CLEAR\r\n");
    } else if (seg_set >= 0 && seg_set <= 9999) {
        segment_display_set_number((uint16_t)seg_set);
        g_seg_display = seg_set;
        printf("[SEG] show %d\r\n", seg_set);
    } else if (seg_set > 9999) {
        segment_display_set_number(9999);
        g_seg_display = 9999;
        printf("[SEG] show 9999 (overflow)\r\n");
    }

    /* ── 回执：有实际动作或查询时发布 ── */
    if (led_set >= 0 || seg_set != -2 || do_query != 0) {
        publish_ack(do_query ? "query" : "ok");
    }
}

/* 回调函数，处理接收到的订阅消息（MQTTX 下发的指令） */
int messageArrived(void *context, char *topicname, int topiclen, MQTTClient_message *message)
{
    char buf[128] = {0};
    int len = message->payloadlen;

    (void)context;
    (void)topiclen;

    /* paho 的 payload 不保证以 '\0' 结尾，先安全拷贝成字符串 */
    if (len > (int)sizeof(buf) - 1) {
        len = (int)sizeof(buf) - 1;
    }
    (void)memcpy_s(buf, sizeof(buf), message->payload, (size_t)len);

    printf("Message arrived on topic: %s\r\n", topicname);
    printf("Message: %s\r\n", buf);

    /* 解析并执行控制指令 */
    parse_and_execute(buf);

    return 1; /* 表示消息已被处理 */
}

/* 周期上报设备状态（MQTTX 订阅 pubTopic 可实时看到设备在线与执行器状态） */
static void report_status(void)
{
    char json[160] = {0};

    (void)snprintf_s(json, sizeof(json), sizeof(json) - 1,
                     "{\"dev\":\"ws63\",\"type\":\"status\",\"led\":%d,"
                     "\"seg\":%d,\"ip\":\"%s\"}",
                     g_led_state, g_seg_display, wifi_get_ip());
    printf("status: %s\r\n", json);
    (void)mqtt_publish(MQTT_TOPIC_PUB, json);
}

static errcode_t mqtt_connect(void)
{
    int ret;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

    /* LED + 数码管的初始化已提前到 mqtt_init_task()（联网之前执行），
     * 这里不再重复初始化。 */

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

    /* 周期上报设备状态（每 5s 一次） */
    while (1) {
        report_status();
        osDelay(STATUS_INTERVAL_TICKS);   /* osDelay 单位为 10ms */
    }
    return ERRCODE_SUCC;
}

void mqtt_init_task(const char *argument)
{
    (void)argument;

    /* 初始化执行器（LED + 数码管）必须放在联网之前：
     * 之前放在 mqtt_connect() 里，WiFi 连不上时数码管永远不初始化（不显示）。
     * 提前初始化后，上电立刻能看到数码管显示 "0"。 */
    led_ctrl_init();
    segment_display_init();

    wifi_connect();
    osDelay(200); /* 延时200ms，等待网络稳定 */
    mqtt_connect();
}

static void wifi_ctrl_mqtt_example(void)
{
    printf("Enter wifi_ctrl_mqtt_example()!");
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

app_run(wifi_ctrl_mqtt_example);
