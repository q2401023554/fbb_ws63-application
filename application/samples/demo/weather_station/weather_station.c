/**
 * 桌面智能气象站 主程序（南向设备开发考核项目）
 *
 * 考核题目（1）："桌面智能气象站" —— DHT11 温湿度采集 + 数码管显示
 *                + Wi-Fi 获取网络天气对比 + MQTT 上报并在 UI 界面显示数据
 *
 * 系统架构（单任务轮询，500ms 节拍）：
 *   ┌────────────────────────────────────────────────────────┐
 *   │ DHT11(每5s) ─┐                                          │
 *   │ HTTP天气(每10min) ├─> 数据看板(串口UI) + JSON(每5s MQTT上报)│
 *   │ QUERY指令    ─┘        + TM1637 数码管轮播(每2s)          │
 *   └────────────────────────────────────────────────────────┘
 *
 * 三层 UI 设计：
 *   1. 数码管轮播 UI：本地温度(带小数点) -> 本地湿度(带小数点)
 *                     -> 网络温度 -> 网络湿度（带 dp = 本地数据，约定）
 *   2. 串口控制台 UI：对比数据看板（本地 vs 网络 vs 偏差 + 运行状态）
 *   3. MQTTX 远程 UI：电脑订阅 pubTopic 查看 JSON 上报，
 *      并可向 subTopic 发送 QUERY 立即触发一次上报
 *
 * 状态 LED（GPIO10）：成功上报心跳闪 1 下；WiFi/MQTT/HTTP 失败快闪 3 下。
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "securec.h"
#include "soc_osal.h"
#include "app_init.h"
#include "cmsis_os2.h"
#include "errcode.h"
#include "app_config.h"
#include "sensor/dht11.h"
#include "display/segment_display.h"
#include "status_led/status_led.h"
#include "wifi/wifi_connect.h"
#include "net/weather_http.h"
#include "mqtt/mqtt_report.h"
#include "uart/uart_cmd.h"

/* 周期换算：秒 -> 主循环 tick 数 */
#define LOCAL_SAMPLE_TICKS   (LOCAL_SAMPLE_INTERVAL_S * 1000 / MAIN_LOOP_TICK_MS)
#define DISPLAY_ROTATE_TICKS (DISPLAY_ROTATE_INTERVAL_S * 1000 / MAIN_LOOP_TICK_MS)
#define NET_REFRESH_TICKS    (NET_REFRESH_INTERVAL_S * 1000 / MAIN_LOOP_TICK_MS)
#define MQTT_REPORT_TICKS    (MQTT_REPORT_INTERVAL_S * 1000 / MAIN_LOOP_TICK_MS)

/* ===== 全局数据（模块间共享）===== */
static dht11_data_t g_local = {0};        /* 本地 DHT11 最近一次有效读数 */
static bool g_local_valid = false;        /* 是否读到过有效数据 */
static weather_data_t g_net = {0};        /* 网络天气最近一次有效读数 */
static bool g_wifi_ok = false;            /* WiFi 连接状态 */
static bool g_mqtt_ok = false;            /* MQTT 连接状态 */
static uint32_t g_report_count = 0;       /* 成功上报次数 */
static uint8_t g_disp_item = 0;           /* 数码管当前轮播项 */
static osThreadId_t g_task_id = NULL;

/* ===== 数码管轮播项 ===== */
typedef enum {
    DISP_ITEM_LOCAL_TEMP = 0,   /* 本地温度（带小数点）*/
    DISP_ITEM_LOCAL_HUMI,       /* 本地湿度（带小数点）*/
    DISP_ITEM_NET_TEMP,         /* 网络温度 */
    DISP_ITEM_NET_HUMI,         /* 网络湿度 */
    DISP_ITEM_MAX
} display_item_t;

static void print_separator(void)
{
    printf("==========================================================\r\n");
}

static void print_banner(void)
{
    print_separator();
    printf("   桌面智能气象站 (Desktop Smart Weather Station)\r\n");
    printf("   DHT11 + TM1637 + WiFi HTTP天气 + MQTT上报\r\n");
    print_separator();
    printf("GPIO10 : DHT11 DATA (单总线)\r\n");
    printf("GPIO13/14: TM1637 数码管 SDA/SCL (板载插槽)\r\n");
    printf("GPIO0  : RGB LED 红通道 (R)\r\n");
    printf("GPIO5  : RGB LED 绿通道 (G)\r\n");
    printf("GPIO7  : RGB LED 蓝通道 (B)\r\n");
    printf("WiFi   : %s\r\n", CONFIG_WIFI_SSID);
    printf("MQTT   : %s:%d  pub=%s sub=%s\r\n",
           CONFIG_MQTT_BROKER_IP, CONFIG_MQTT_BROKER_PORT, MQTT_TOPIC_PUB, MQTT_TOPIC_SUB);
    printf("UART   : QUERY 立即上报 / NET 刷新天气 / SHOW 切换显示 / LEDTEST 扫脚测灯\r\n");
    print_separator();
}

/**
 * @brief 刷新网络天气（失败时 LED 快闪 3 下提示）
 */
static void ws_net_refresh(void)
{
    if (weather_http_fetch(&g_net) != 0) {
        printf("[NET] weather fetch FAIL, keep last data\r\n");
        status_led_error();
    }
}

/**
 * @brief 采集一次本地温湿度（失败时保留上次有效值）
 */
static void ws_read_local(void)
{
    dht11_data_t tmp = {0};
    if (dht11_read(&tmp) == 0) {
        g_local = tmp;
        g_local_valid = true;
        printf("[DHT11] temp=%d humi=%d\r\n", g_local.temperature, g_local.humidity);
    } else {
        printf("[DHT11] read FAIL, keep last (%s)\r\n",
               g_local_valid ? "valid" : "none");
    }
}

/**
 * @brief 数码管轮播：跳到下一个"有数据可显示"的项
 */
static void ws_display_rotate(void)
{
    uint8_t tries = 0;
    uint8_t item = g_disp_item;

    while (tries < DISP_ITEM_MAX) {
        item = (uint8_t)((item + 1) % DISP_ITEM_MAX);
        tries++;
        if ((item == DISP_ITEM_LOCAL_TEMP || item == DISP_ITEM_LOCAL_HUMI) && g_local_valid) {
            break;
        }
        if ((item == DISP_ITEM_NET_TEMP || item == DISP_ITEM_NET_HUMI) && g_net.valid) {
            break;
        }
    }

    switch (item) {
        case DISP_ITEM_LOCAL_TEMP:
            segment_display_set_number_dp(g_local.temperature, true);
            break;
        case DISP_ITEM_LOCAL_HUMI:
            segment_display_set_number_dp(g_local.humidity, true);
            break;
        case DISP_ITEM_NET_TEMP:
            segment_display_set_number((uint16_t)g_net.temp);
            break;
        case DISP_ITEM_NET_HUMI:
            segment_display_set_number((uint16_t)g_net.humi);
            break;
        default:
            break;
    }
    g_disp_item = item;
}

/**
 * @brief 串口控制台 UI：打印本地 vs 网络对比数据看板
 */
static void ws_dashboard_print(void)
{
    print_separator();
    printf("  桌面智能气象站 · 数据看板 (第 %u 次上报)\r\n", (unsigned int)g_report_count + 1);
    print_separator();

    if (g_local_valid) {
        printf("  本地采集  DHT11      : 温度 %d ℃   湿度 %d %%RH\r\n",
               g_local.temperature, g_local.humidity);
    } else {
        printf("  本地采集  DHT11      : 暂无有效数据\r\n");
    }

    if (g_net.valid) {
        printf("  网络天气  Open-Meteo : 温度 %d ℃   湿度 %d %%RH\r\n", g_net.temp, g_net.humi);
    } else {
        printf("  网络天气  Open-Meteo : 获取失败\r\n");
    }

    /* 双方都有数据时输出偏差（本地 - 网络，正数表示本地偏高）*/
    if (g_local_valid && g_net.valid) {
        printf("  对比结果             : 温差 %+d ℃   湿度差 %+d %%\r\n",
               (int)g_local.temperature - g_net.temp,
               (int)g_local.humidity - g_net.humi);
    }
    printf("  运行状态             : WiFi=%s  MQTT=%s  天气=%s\r\n",
           g_wifi_ok ? "OK" : "FAIL",
           g_mqtt_ok ? "OK" : "FAIL",
           g_net.valid ? "OK" : "FAIL");
    print_separator();
}

/**
 * @brief 组装上报 JSON 并发布，同时刷新串口看板
 *
 * JSON 示例：
 * {"dev":"ws63-weather","temp":26,"humi":58,"netTemp":25,"netHumi":60,
 *  "diffTemp":1,"diffHumi":-2,"netOk":1,"ip":"192.168.43.100"}
 */
static void ws_report(void)
{
    char json[192] = {0};
    int32_t net_t = g_net.valid ? g_net.temp : -999;
    int32_t net_h = g_net.valid ? g_net.humi : -999;
    /* 偏差值只在"本地和网络两边都有效"时才计算。
     * 否则 net_t = -999 会算出 29-(-999)=1028 这种荒谬数字，直接出现在看板上。
     * 无效时统一填 -999，上位机看到 -999 就显示 --。 */
    bool diff_valid = (g_local_valid && g_net.valid);
    int32_t diff_t = diff_valid ? ((int32_t)g_local.temperature - net_t) : -999;
    int32_t diff_h = diff_valid ? ((int32_t)g_local.humidity - net_h) : -999;

    (void)snprintf_s(json, sizeof(json), sizeof(json) - 1,
                     "{\"dev\":\"ws63-weather\",\"temp\":%d,\"humi\":%d,"
                     "\"netTemp\":%d,\"netHumi\":%d,"
                     "\"diffTemp\":%d,\"diffHumi\":%d,"
                     "\"netOk\":%d,\"ip\":\"%s\"}",
                     g_local.temperature, g_local.humidity,
                     net_t, net_h,
                     diff_t, diff_h,
                     g_net.valid ? 1 : 0,
                     wifi_get_ip());

    ws_dashboard_print();
    printf("[MQTT] report: %s\r\n", json);

    if (mqtt_report_publish(json) == 0) {
        g_report_count++;
        status_led_heartbeat();        /* 心跳：成功上报闪 1 下 */
    } else {
        printf("[MQTT] publish FAIL (broker offline?), will retry\r\n");
        g_mqtt_ok = false;
        status_led_error();            /* 异常：快闪 3 下 */
    }
}

static void ws_main_task(const char *arg)
{
    (void)arg;
    uint32_t tick = 0;

    print_banner();

    /* ---------- 1. 本地外设初始化 ---------- */
    segment_display_init();
    status_led_init();
    if (dht11_init() != 0) {
        printf("WARN: dht11 init failed, temp/humi will stay 0\r\n");
        status_led_error();
    }

    /* ---------- 1.5 串口指令接收（PC 下发指令响应，考核要求）---------- */
    if (uart_cmd_init() != 0) {
        printf("WARN: uart cmd init failed, serial control disabled\r\n");
    }

    /* ---------- 2. WiFi 联网（内部自动重试扫描连接）---------- */
    if (wifi_connect() == ERRCODE_SUCC) {
        g_wifi_ok = true;
    } else {
        printf("WARN: wifi connect failed, net features disabled\r\n");
        status_led_error();
    }

    /* ---------- 3. 首次获取网络天气 ---------- */
    if (g_wifi_ok) {
        ws_net_refresh();
    }

    /* ---------- 4. 连接 MQTT（最多重试 3 次，失败不影响本地功能）---------- */
    if (g_wifi_ok) {
        for (uint8_t i = 0; i < 3; i++) {
            if (mqtt_report_connect() == 0) {
                g_mqtt_ok = true;
                break;
            }
            printf("[MQTT] retry %d/3 after 3s...\r\n", i + 1);
            osal_msleep(3000);
        }
        if (!g_mqtt_ok) {
            status_led_error();
        }
    }

    printf("[READY] weather station started\r\n");

    /* ---------- 5. 主循环：500ms 节拍轮询 ---------- */
    while (1) {
        osal_msleep(MAIN_LOOP_TICK_MS);
        tick++;

        /* 泵 MQTT 收发，处理保活与远程 QUERY 指令 */
        mqtt_report_poll();

        /* 泵串口指令，处理 PC 下发的 QUERY/NET/SHOW */
        uart_cmd_poll();

        /* MQTT 断线重连：连接丢失回调置位后，这里负责重建连接 */
        if (mqtt_report_need_reconnect()) {
            g_mqtt_ok = false;
            if (mqtt_report_reconnect() == 0) {
                g_mqtt_ok = true;
            }
        }

        /* 串口 LEDTEST：依次点亮候选 GPIO，用于排查外接灯接线（排障用）*/
        if (uart_cmd_take_ledtest_flag()) {
            status_led_sweep_test();
        }

        /* 周期采集本地温湿度 */
        if ((tick % LOCAL_SAMPLE_TICKS) == 0) {
            ws_read_local();
        }

        /* 数码管轮播（或收到串口 SHOW 指令立即切换）*/
        if (((tick % DISPLAY_ROTATE_TICKS) == 0) || uart_cmd_take_show_flag()) {
            ws_display_rotate();
        }

        /* 周期刷新网络天气（或收到串口 NET 指令立即刷新）*/
        if (g_wifi_ok && (((tick % NET_REFRESH_TICKS) == 0) || uart_cmd_take_net_flag())) {
            ws_net_refresh();
        }

        /* 周期上报；收到远程 QUERY 指令（MQTT 或串口）时立即上报一次 */
        if (((tick % MQTT_REPORT_TICKS) == 0) ||
            mqtt_report_take_query_flag() || uart_cmd_take_query_flag()) {
            ws_report();
        }
    }
}

static void weather_station_entry(void)
{
    osThreadAttr_t options;

    options.name = "weather_station";
    options.attr_bits = 0;
    options.cb_mem = NULL;
    options.cb_size = 0;
    options.stack_mem = NULL;
    options.stack_size = WS_TASK_STACK_SIZE;   /* HTTP 缓冲 + MQTT 收发，需要较大栈 */
    options.priority = osPriorityNormal;

    g_task_id = osThreadNew((osThreadFunc_t)ws_main_task, NULL, &options);
    if (g_task_id != NULL) {
        printf("Create weather_station task OK\r\n");
    } else {
        printf("ERROR: create weather_station task FAILED!\r\n");
    }
}

/* Run the weather station. */
app_run(weather_station_entry);
