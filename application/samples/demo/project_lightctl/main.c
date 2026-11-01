/**
 * Wi-Fi 远程灯控系统 —— 主程序
 *
 * 对应《智能设备创新实践》期末项目参考题目 2.(1)：
 *   「MQTT 订阅控制指令 + 数码管状态显示 + PWM 亮度调节 + MQTT 上报并使用 UI 界面控制」
 *
 * ─── 系统组成 ───
 *   RGB 灯（共阴 4 针，R=P10 / G=P8 / B=P5，三路硬件 PWM）—— 被控对象，亮度 0~100
 *   光敏电阻（AO = P12，ADC 通道 5）                        —— 环境光采集
 *   TM1637 数码管（CLK=P14 / DIO=P13，插右侧扩展板插座）    —— 本地状态显示
 *   WiFi + MQTT（broker 跑在电脑上）                        —— 与网页 UI 双向通信
 *
 * ─── 接线（Pxx 编号 == GPIO 编号）───
 *   RGB 灯（共阴）: R -> P10    G -> P8     B -> P5     公共阴极 -> GND
 *   光敏电阻      : VCC -> 3.3V  GND -> GND  AO -> P12
 *   数码管        : 插在右侧扩展板插座上，由插座供电，无需额外接线
 *   显示选择器    : P2 输出 0、P9 输出 1（软件设置，无需接导线）
 *
 * ─── 报文 ───
 *   下行 light/control ：{"on":1}  {"on":0}  {"bright":50}
 *   上行 light/status  ：{"on":1,"bright":80,"lux":46}
 */
#include <stdio.h>
#include "soc_osal.h"
#include "app_init.h"
#include "cmsis_os2.h"
#include "common_def.h"
#include "securec.h"
#include "actuator/pwm_led.h"
#include "actuator/tm1637.h"
#include "sensor/light_adc.h"
#include "mqtt/mqtt_client.h"
#include "wifi/wifi_connect.h"

/* ---------------- 时间参数（osDelay(1) == 10ms）---------------- */
#define MAIN_TICK_DELAY    10U   /* 主循环节拍：10 × 10ms = 100ms */
#define TICKS_LIGHT_READ   10U   /* 每 10 拍（1s）读一次环境光 */
#define TICKS_DISPLAY      20U   /* 每 20 拍（2s）数码管翻一页 */
#define TICKS_REPORT       20U   /* 每 20 拍（2s）周期上报一次状态 */
#define TICKS_MQTT_CHECK   50U   /* 每 50 拍（5s）检查一次 MQTT 连接 */

/* 数码管页面数：0 = 开关+亮度，1 = 环境光 */
#define DISPLAY_SLOT_NUM   2U

/* 线程栈（MQTT 收发较吃内存） */
#define LIGHTCTL_TASK_STACK 0x3000

/* ---------------- 全局状态 ---------------- */
static volatile uint8_t g_lux = 0;      /* 最近一次环境光百分比 0~100 */
static volatile uint8_t g_dirty = 0;    /* 指令刚改过状态，需要立即上报 */
static uint8_t g_slot = 0;              /* 当前数码管页面 */

/* ---------------- 数码管显示 ---------------- */

/**
 * @brief 刷新数码管当前页面
 * @param slot 0 = 开关与亮度，1 = 环境光
 *
 * 页面 0：开灯显示 "b80"（亮度 80%），关灯显示 "OFF"
 * 页面 1：显示 "L46"（环境光 46）
 */
static void lightctl_show_slot(uint8_t slot)
{
    char text[8] = {0};
    uint8_t on = 0;
    uint8_t bright = 0;

    if (slot == 0U) {
        pwm_led_get_state(&on, &bright);
        if (on != 0U) {
            (void)snprintf_s(text, sizeof(text), sizeof(text) - 1, "b%d", (int)bright);
        } else {
            (void)snprintf_s(text, sizeof(text), sizeof(text) - 1, "OFF");
        }
    } else {
        (void)snprintf_s(text, sizeof(text), sizeof(text) - 1, "L%d", (int)g_lux);
    }
    tm1637_show_text(text);
}

/* ---------------- 状态上报 ---------------- */

/**
 * @brief 打包并上报一次状态（MQTT 未连接时直接返回）
 * @param event 事件名，仅用于串口打印
 */
static void lightctl_report(const char *event)
{
    char json[96] = {0};
    uint8_t on = 0;
    uint8_t bright = 0;

    if (mqtt_client_is_connected() == 0U) {
        return;
    }

    pwm_led_get_state(&on, &bright);
    (void)snprintf_s(json, sizeof(json), sizeof(json) - 1,
                     "{\"on\":%d,\"bright\":%d,\"lux\":%d}",
                     (int)on, (int)bright, (int)g_lux);

    printf("lightctl: report(%s) %s\r\n", event, json);
    (void)mqtt_client_publish_str(MQTT_TOPIC_PUB, json);
}

/* ---------------- 指令处理 ---------------- */

/**
 * @brief 处理电脑端下发的一条控制指令（在 MQTT 回调线程中被调用）
 * @param payload 报文文本
 */
static void lightctl_handle_command(const char *payload)
{
    int ival = 0;
    uint8_t changed = 0;

    printf("lightctl: handle command: %s\r\n", payload);

    /* 开关：{"on":1} / {"on":0} */
    if (mqtt_json_find_int(payload, "on", &ival) != 0) {
        (void)pwm_led_set_on((ival != 0) ? 1U : 0U);
        changed = 1U;
    }

    /* 亮度：{"bright":50}，越界自动截断到 0~100 */
    if (mqtt_json_find_int(payload, "bright", &ival) != 0) {
        if (ival < 0) {
            ival = 0;
        } else if (ival > 100) {
            ival = 100;
        }
        (void)pwm_led_set_bright((uint8_t)ival);
        changed = 1U;
    }

    if (changed == 0U) {
        printf("lightctl: no known key in payload, ignored\r\n");
        return;
    }

    /* 立刻刷新数码管，让本地显示与远端操作同步；
     * 上报交给主循环，避免在回调线程里做网络发送。 */
    lightctl_show_slot(g_slot);
    g_dirty = 1U;
}

/* ---------------- 主循环 ---------------- */

static void lightctl_main_loop(void)
{
    uint32_t tick = 0;

    lightctl_show_slot(g_slot);

    while (1) {
        tick++;

        /* 每 1s：读一次环境光 */
        if ((tick % TICKS_LIGHT_READ) == 0U) {
            light_adc_data_t lt = {0};
            if (light_adc_read(&lt) == 0) {
                g_lux = lt.percent;
            } else {
                printf("lightctl: light adc read failed\r\n");
            }
        }

        /* 每 2s：数码管翻页（并顺带刷新数值） */
        if ((tick % TICKS_DISPLAY) == 0U) {
            g_slot = (uint8_t)((g_slot + 1U) % DISPLAY_SLOT_NUM);
            lightctl_show_slot(g_slot);
        }

        /* 每 2s，或指令刚改过状态：上报 */
        if (((tick % TICKS_REPORT) == 0U) || (g_dirty != 0U)) {
            lightctl_report((g_dirty != 0U) ? "change" : "period");
            g_dirty = 0U;
        }

        /* 每 5s：确认 MQTT 连接，断了就重连 */
        if ((tick % TICKS_MQTT_CHECK) == 0U) {
            if (mqtt_client_is_connected() == 0U) {
                printf("lightctl: mqtt not connected, retry...\r\n");
                (void)mqtt_client_connect();
            }
        }

        osDelay(MAIN_TICK_DELAY);
    }
}

/* ---------------- 启动流程 ---------------- */

static void lightctl_task(const char *arg)
{
    unused(arg);

    printf("\r\n===== WS63 Wi-Fi 远程灯控系统 启动 =====\r\n");

    /* 1. 外设初始化：断网时也能凭串口与灯判断硬件是否正常 */
    if (pwm_led_init() != 0) {
        printf("WARN: pwm led init failed, light will not work\r\n");
    }

    if (tm1637_init() != 0) {
        printf("WARN: tm1637 init failed, display will not work\r\n");
    } else {
        /* 4 位全亮 1s：既能确认接线，也能确认显示选择器已生效 */
        printf("tm1637 self-test: all segments ON for 1s ...\r\n");
        tm1637_display_test(1U);
        osDelay(10U);
        tm1637_display_test(0U);
    }

    if (light_adc_init() != 0) {
        printf("WARN: light adc init failed, lux will stay 0\r\n");
    }

    /* 2. 联网（内部会一直重试到连上为止） */
    (void)wifi_connect();

    /* 3. 连接 MQTT；失败也不退出，主循环会持续重连 */
    mqtt_client_set_cmd_callback(lightctl_handle_command);
    if (mqtt_client_connect() != 0) {
        printf("WARN: mqtt connect failed, will retry in main loop\r\n");
    } else {
        lightctl_report("online");
    }

    /* 4. 主循环 */
    lightctl_main_loop();
}

static void lightctl_entry(void)
{
    osThreadAttr_t attr;
    osThreadId_t tid;

    attr.name = "lightctl_task";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = LIGHTCTL_TASK_STACK;
    attr.priority = osPriorityNormal;

    tid = osThreadNew((osThreadFunc_t)lightctl_task, NULL, &attr);
    if (tid != NULL) {
        printf("lightctl: task created OK\r\n");
    } else {
        printf("lightctl: task create FAILED\r\n");
    }
}

app_run(lightctl_entry);
