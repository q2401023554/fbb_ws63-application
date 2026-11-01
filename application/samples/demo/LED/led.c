/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 *
 * Description: LED sample source (RGB LED auto-flash on P0/P6/P9 at boot). \n
 *
 * History: \n
 * 2026-09-16, Create file (original auto-flash version, moved from LED2). \n
 *
 * @note  This sample flashes automatically at boot.
 *        It shares P0/P6/P9 with the LED2 sample (UART controlled),
 *        so enable only ONE of them at a time in menuconfig.
 */

#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"
#include "app_init.h"
#include "led.h"

/* RGB 三色 LED 引脚定义：P0 红、P6 绿、P9 蓝 */
#define LED_PIN_RED         0   /* P0：红色通道 */
#define LED_PIN_GREEN       6   /* P6：绿色通道 */
#define LED_PIN_BLUE        9   /* P9：蓝色通道 */

/* 单色显示时长（毫秒） */
#define LED_FLASH_PERIOD_MS     500

/* 闪烁任务优先级与栈大小 */
#define LED_TASK_PRIO           24
#define LED_TASK_STACK_SIZE     0x1000

/* 单个引脚配置助手：复用为 GPIO + 输出方向 + 初始低电平，失败时打印 */
static void led_pin_setup(pin_t pin)
{
    errcode_t ret;

    ret = uapi_pin_set_mode(pin, HAL_PIO_FUNC_GPIO);
    if (ret != ERRCODE_SUCC) {
        osal_printk("LED: pin %d set mode FAIL, ret=%d\r\n", pin, ret);
    }

    ret = uapi_gpio_set_dir(pin, GPIO_DIRECTION_OUTPUT);
    if (ret != ERRCODE_SUCC) {
        osal_printk("LED: pin %d set dir FAIL, ret=%d\r\n", pin, ret);
    }

    ret = uapi_gpio_set_val(pin, GPIO_LEVEL_LOW);
    if (ret != ERRCODE_SUCC) {
        osal_printk("LED: pin %d set val FAIL, ret=%d\r\n", pin, ret);
    }
}

/* 将 P0/P6/P9 复用为 GPIO 功能，并配置为推挽输出 */
void led_init(void)
{
    /* 引脚复用 + 方向 + 初始电平：三个通道全部配置 */
    led_pin_setup(LED_PIN_RED);
    led_pin_setup(LED_PIN_GREEN);
    led_pin_setup(LED_PIN_BLUE);

    /* 初始状态：全灭。
     * 默认按共阴接法（公共端接地，高电平点亮）。
     * 若为共阳接法（公共端接电源，低电平点亮），将 led_set_color()
     * 中的电平全部取反即可。 */
    led_set_color(LED_COLOR_OFF);
}

/* 按颜色枚举设置三通道电平 */
void led_set_color(led_color_t color)
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    switch (color) {
        case LED_COLOR_RED:
            r = 1;
            break;
        case LED_COLOR_GREEN:
            g = 1;
            break;
        case LED_COLOR_BLUE:
            b = 1;
            break;
        case LED_COLOR_WHITE:
            r = 1;
            g = 1;
            b = 1;
            break;
        case LED_COLOR_OFF:
        default:
            break;
    }

    uapi_gpio_set_val(LED_PIN_RED,   r ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
    uapi_gpio_set_val(LED_PIN_GREEN, g ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
    uapi_gpio_set_val(LED_PIN_BLUE,  b ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

/* 同时翻转三通道输出电平（兼容旧接口） */
void led_flash(void)
{
    uapi_gpio_toggle(LED_PIN_RED);
    uapi_gpio_toggle(LED_PIN_GREEN);
    uapi_gpio_toggle(LED_PIN_BLUE);
}

/* 持续三色循环演示：红 -> 绿 -> 蓝 -> 红 ...（阻塞，在任务中运行） */
void led_flash_forever(void)
{
    while (1) {
        led_set_color(LED_COLOR_RED);
        osal_printk("RGB LED: RED\r\n");
        osal_msleep(LED_FLASH_PERIOD_MS);

        led_set_color(LED_COLOR_GREEN);
        osal_printk("RGB LED: GREEN\r\n");
        osal_msleep(LED_FLASH_PERIOD_MS);

        led_set_color(LED_COLOR_BLUE);
        osal_printk("RGB LED: BLUE\r\n");
        osal_msleep(LED_FLASH_PERIOD_MS);
    }
}

/* 闪烁任务入口：先初始化引脚，再进入循环 */
static int led_task(const char *arg)
{
    unused(arg);

    led_init();
    led_flash_forever();

    return 0;
}

/* 创建 LED 闪烁任务 */
static void led_entry(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)led_task, 0, "LEDTask",
                                      LED_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, LED_TASK_PRIO);
    }
    osal_kthread_unlock();
}

/* 系统启动后自动运行 RGB 三色循环 */
app_run(led_entry);
