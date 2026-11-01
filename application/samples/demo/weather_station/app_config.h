/**
 * 桌面智能气象站 —— 引脚与参数集中配置
 *
 * 考核题目：DHT11 温湿度采集 + 数码管显示 + Wi-Fi 获取网络天气对比 + MQTT 上报并在 UI 界面显示数据
 * 设计原则：所有接线只改本文件即可适配，不用翻驱动源码。
 *
 * ⚠ 引脚冲突说明（重要）：
 *   板载数码管插槽硬件固定占用 GPIO13/14（TM1637），
 *   GPIO9 是插槽模式选择线（必须恒为 1，不能给其它外设用）。
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "gpio.h"

/* ===== DHT11 温湿度传感器（3 针模块，VCC->3.3V，GND->GND）===== */
#define DHT11_DATA_PIN              GPIO_10     /* DATA -> P10，单总线数据 */

/* ===== TM1637 四位数码管（板载插槽，硬件固定）===== */
#define TM1637_SDA_PIN      GPIO_13     /* SDA -> GPIO13 */
#define TM1637_SCL_PIN      GPIO_14     /* SCL -> GPIO14 */

/* ===== 板载数码管/OLED 插槽模式选择（数码管模式必须 GPIO2=0、GPIO9=1）===== */
#define DISPLAY_SELECTOR_PIN2   GPIO_02
#define DISPLAY_SELECTOR_PIN9   GPIO_09

/* ===== 状态指示 RGB LED（外接 4 针模块：R / G / B + 公共脚）=====
 * 三路信号分别占用三个独立 GPIO，用三路电平组合出颜色：
 *   绿色 = 心跳（每次成功上报闪一下）
 *   红色 = 异常（WiFi/HTTP/MQTT 失败快闪 3 下）
 *   蓝色 = 开机自检的单路验证
 *
 * 接线（共阴模块，最常见，公共脚标 GND / "-"）：
 *   R  -> P0 (GPIO0)
 *   G  -> P5 (GPIO5)
 *   B  -> P7 (GPIO7)
 *   公共脚 GND -> GND
 *
 * 若为共阳模块（公共脚标 VCC / "+"）：公共脚接 3.3V，
 * 并把下面 LED_RGB_COMMON_ANODE 改成 1（驱动会自动把电平取反）。
 *
 * 选脚依据：P0/P5/P7 在 SDK 里均无功能占用，本工程也只用到
 *   P2/P9（数码管插槽选择）、P10（DHT11）、P13/P14（TM1637）、P17/P18（UART），互不冲突。 */
#define LED_RGB_R_PIN           GPIO_00     /* 红色通道 -> P0 */
#define LED_RGB_G_PIN           GPIO_05     /* 绿色通道 -> P5 */
#define LED_RGB_B_PIN           GPIO_07     /* 蓝色通道 -> P7 */

/* 0 = 共阴（公共脚接 GND，高电平点亮）；1 = 共阳（公共脚接 3.3V，低电平点亮）*/
#define LED_RGB_COMMON_ANODE    0

#if (LED_RGB_COMMON_ANODE > 0)
#define LED_RGB_ON_LEVEL        GPIO_LEVEL_LOW
#else
#define LED_RGB_ON_LEVEL        GPIO_LEVEL_HIGH
#endif

/* 开机自检：上电依次点亮 红->绿->蓝，再全亮 2 次，用来确认三路接线是否都通。
 * 哪一路不亮就是那一路的线/针脚问题；三路全不亮 = 公共脚没接或模块没供电。
 * 设为 0 可关闭自检。 */
#define LED_STATUS_SELFTEST_TIMES   2       /* 三色轮完一遍后，全亮闪烁的次数 */
#define LED_STATUS_SELFTEST_ON_MS   400     /* 单色点亮时长（长一点方便肉眼确认）*/
#define LED_STATUS_SELFTEST_OFF_MS  200

/* 运行期闪烁参数 */
#define LED_STATUS_REPORT_ON_MS     200   /* 心跳（绿）亮灯时长，太短肉眼易漏看 */
#define LED_STATUS_ERROR_ON_MS      150   /* 异常（红）亮灯时长 */
#define LED_STATUS_ERROR_OFF_MS     150   /* 异常（红）灭灯时长 */

/* 排障开关：设为 1 时上电自动做一次扫脚测试（逐路 + 逐脚各亮 1 秒）。
 * 平时保持 0；串口助手发不了指令时，改成 1 重编译即可观察。
 * 串口发 LEDTEST 也能随时触发，不必改这里。 */
#define LED_STATUS_SWEEP_AT_BOOT    0

/* ===== 任务参数 ===== */
#define WS_TASK_STACK_SIZE  0x4000      /* 主任务栈：HTTP 缓冲 + MQTT 收发，需较大栈 */
#define MAIN_LOOP_TICK_MS   500         /* 主循环节拍：500ms 走一格 */

/* ===== 周期参数（单位：秒，主循环自动换算成 tick 数）===== */
#define LOCAL_SAMPLE_INTERVAL_S   5     /* DHT11 采集周期（单总线要求 >=1s，取 5s）*/
#define DISPLAY_ROTATE_INTERVAL_S 2     /* 数码管轮播切换周期 */
#define NET_REFRESH_INTERVAL_S    600   /* 网络天气刷新周期（10 分钟，开放接口允许）*/
#define MQTT_REPORT_INTERVAL_S    5     /* MQTT 上报周期 */

/* ===== MQTT 服务器 ===== */
/* 填运行 EMQX 的那台电脑在局域网中的 IP，板子与电脑须在同一网段 */
#define CONFIG_MQTT_BROKER_IP   "192.168.1.100"
#define CONFIG_MQTT_BROKER_PORT 1883
#define CONFIG_MQTT_CLIENT_ID   "ws63-weather"
#define MQTT_TOPIC_PUB          "pubTopic"      /* 上报主题：板子 -> MQTTX */
#define MQTT_TOPIC_SUB          "subTopic"      /* 订阅主题：MQTTX -> 板子（发 QUERY 立即上报）*/

#endif /* APP_CONFIG_H */
