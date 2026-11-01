/**
 * 实验六（综合项目开发实践二）引脚配置
 *
 * 只需在这里改宏，即可适配不同接线。
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "gpio.h"

/* ===== 外接 LED 模块控制引脚 =====
 * 高电平点亮。接法：LED 模块 IN 引脚 -> GPIO10（或你板子上任意空闲 GPIO），
 * 模块 VCC -> 3.3V / 5V，GND -> GND。
 * 若改接线，只需修改下面这一个宏。 */
#define LED_CTRL_PIN GPIO_10

/* ===== TM1637 数码管（板载插槽）=====
 * 重要：板载数码管插槽硬件固定走 GPIO13/14（与 weather_station / smart_light_monitor
 * 已烧录验证的接法一致），不可使用 OLED 例程的 GPIO15/16 —— 那是 I2C1 硬件控制器
 * （pin mode 2）驱动 SSD1306 OLED 的走线，TM1637 位操作时序必须用普通 GPIO 模拟。
 * 引脚对应：TM1637 的 CLK -> GPIO14(SCL)、DIO -> GPIO13(SDA)。 */
#define TM1637_SDA_PIN GPIO_13
#define TM1637_SCL_PIN GPIO_14

/* ===== 板载数码管/OLED 模式选择 =====
 * 数码管插在板载插槽时，selector 必须配置为 GPIO2=0、GPIO9=1。 */
#define DISPLAY_SELECTOR_PIN2 GPIO_02
#define DISPLAY_SELECTOR_PIN9 GPIO_09

#endif /* APP_CONFIG_H */
