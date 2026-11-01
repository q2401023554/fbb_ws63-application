/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 *
 * Description: LED2 sample source (RGB LED on P0/P6/P9). \n
 *
 * History: \n
 * 2026-09-15, Create file. \n
 * 2026-09-16, Add P3 green channel and color control interface. \n
 * 2026-09-16, Change green channel from P3 to P6. \n
 * 2026-09-16, Remove the 1s green self-check after debugging. \n
 * 2026-09-16, Rework to library: flash task controlled by start/stop API, no auto run. \n
 */

#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"
#include "led2.h"

/* RGB 三色 LED 引脚定义：P0 红、P6 绿、P9 蓝 */
#define LED2_PIN_RED        0   /* P0：红色通道 */
#define LED2_PIN_GREEN      6   /* P6：绿色通道 */
#define LED2_PIN_BLUE       9   /* P9：蓝色通道 */

/* 单色显示时长（毫秒） */
#define LED2_FLASH_PERIOD_MS    500

/* 闪烁任务优先级与栈大小 */
#define LED2_TASK_PRIO          24
#define LED2_TASK_STACK_SIZE    0x1000

/* 闪烁任务句柄：非 NULL 表示正在闪烁 */
static osal_task *g_led_task = NULL;

/* 单个引脚配置助手：复用为 GPIO + 输出方向 + 初始低电平，失败时打印 */
static void led2_pin_setup(pin_t pin)
{
    errcode_t ret;

    ret = uapi_pin_set_mode(pin, HAL_PIO_FUNC_GPIO);
    if (ret != ERRCODE_SUCC) {
        osal_printk("LED2: pin %d set mode FAIL, ret=%d\r\n", pin, ret);
    }

    ret = uapi_gpio_set_dir(pin, GPIO_DIRECTION_OUTPUT);
    if (ret != ERRCODE_SUCC) {
        osal_printk("LED2: pin %d set dir FAIL, ret=%d\r\n", pin, ret);
    }

    ret = uapi_gpio_set_val(pin, GPIO_LEVEL_LOW);
    if (ret != ERRCODE_SUCC) {
        osal_printk("LED2: pin %d set val FAIL, ret=%d\r\n", pin, ret);
    }
}

/* 将 P0/P6/P9 复用为 GPIO 功能，并配置为推挽输出 */
void led2_init(void)
{
    /* 引脚复用 + 方向 + 初始电平：三个通道全部配置 */
    led2_pin_setup(LED2_PIN_RED);
    led2_pin_setup(LED2_PIN_GREEN);
    led2_pin_setup(LED2_PIN_BLUE);

    /* 初始状态：全灭。
     * 默认按共阴接法（公共端接地，高电平点亮）。
     * 若为共阳接法（公共端接电源，低电平点亮），将 led2_set_color()
     * 中的电平全部取反即可。 */
    led2_set_color(LED2_COLOR_OFF);
}

/* 按颜色枚举设置三通道电平 */
void led2_set_color(led2_color_t color)
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    switch (color) {
        case LED2_COLOR_RED:
            r = 1;
            break;
        case LED2_COLOR_GREEN:
            g = 1;
            break;
        case LED2_COLOR_BLUE:
            b = 1;
            break;
        case LED2_COLOR_WHITE:
            r = 1;
            g = 1;
            b = 1;
            break;
        case LED2_COLOR_OFF:
        default:
            break;
    }

    uapi_gpio_set_val(LED2_PIN_RED,   r ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
    uapi_gpio_set_val(LED2_PIN_GREEN, g ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
    uapi_gpio_set_val(LED2_PIN_BLUE,  b ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

/* 同时翻转三通道输出电平（兼容旧接口） */
void led2_flash(void)
{
    uapi_gpio_toggle(LED2_PIN_RED);
    uapi_gpio_toggle(LED2_PIN_GREEN);
    uapi_gpio_toggle(LED2_PIN_BLUE);
}

/* 后台闪烁任务：红 -> 绿 -> 蓝 循环，直到任务被 led2_flash_stop() 销毁 */
static int led2_flash_task(const char *arg)
{
    unused(arg);

    for (;;) {
        led2_set_color(LED2_COLOR_RED);
        osal_printk("RGB LED: RED\r\n");
        osal_msleep(LED2_FLASH_PERIOD_MS);

        led2_set_color(LED2_COLOR_GREEN);
        osal_printk("RGB LED: GREEN\r\n");
        osal_msleep(LED2_FLASH_PERIOD_MS);

        led2_set_color(LED2_COLOR_BLUE);
        osal_printk("RGB LED: BLUE\r\n");
        osal_msleep(LED2_FLASH_PERIOD_MS);
    }

    return 0;
}

/* 启动 RGB 闪烁（非阻塞，重复调用无效果） */
void led2_flash_start(void)
{
    osal_task *task_handle = NULL;

    if (g_led_task != NULL) {
        return;   /* 已在闪烁，忽略重复启动 */
    }

    led2_init();   /* 复用引脚并配置为输出 */

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)led2_flash_task, NULL, "LED2Task",
                                      LED2_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, LED2_TASK_PRIO);
    }
    osal_kthread_unlock();

    g_led_task = task_handle;
}

/* 停止 RGB 闪烁并关灯（非阻塞，重复调用无效果） */
void led2_flash_stop(void)
{
    if (g_led_task != NULL) {
        osal_kthread_destroy(g_led_task, 0);   /* 销毁闪烁任务 */
        g_led_task = NULL;
    }
    led2_set_color(LED2_COLOR_OFF);   /* 关灯 */
}
