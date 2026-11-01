#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 初始化状态指示 RGB LED（外接 4 针模块，R/G/B 三路各占一个 GPIO）
 *
 * 接线：R->P0、G->P5、B->P7，公共脚按共阴接 GND
 *       （共阳模块公共脚接 3.3V，并把 app_config.h 的 LED_RGB_COMMON_ANODE 改成 1）。
 *
 * 初始化完成后会做一次自检：红 -> 绿 -> 蓝 -> 全亮闪 N 次，
 * 哪一路不亮就是那一路的接线问题，三路全不亮通常是公共脚没接或模块未供电。
 */
void status_led_init(void);

/**
 * @brief 三路全亮 / 全灭
 */
void status_led_set(bool on);

/**
 * @brief 阻塞式闪烁（三路同亮，呈白色）
 * @param times  闪烁次数
 * @param on_ms  亮灯时长（毫秒）
 * @param off_ms 灭灯时长（毫秒）
 */
void status_led_blink(uint8_t times, uint32_t on_ms, uint32_t off_ms);

/**
 * @brief 心跳提示：每次成功获取/上报一次结果，绿灯闪一下
 */
void status_led_heartbeat(void);

/**
 * @brief 异常提示：WiFi / HTTP / MQTT 任一环节失败时，红灯快闪 3 下
 */
void status_led_error(void);

/**
 * @brief 扫脚测试（排障用，串口发 LEDTEST 触发）
 *
 * 第一段逐路验证：红、绿、蓝各亮 1 秒，确认三路接线；
 * 第二段逐脚验证：P0/P1/P3/P4/P5/P7/P8/P11/P12 各亮 1 秒，
 * 用于换孔插线时判断哪个针脚真的有输出。
 */
void status_led_sweep_test(void);

#endif
