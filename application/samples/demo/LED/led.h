/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 *
 * Description: LED sample header (RGB auto-flash on P0/P6/P9 at boot). \n
 *
 * History: \n
 * 2026-09-16, Create file (original auto-flash version, moved from LED2). \n
 */

#ifndef LED_H
#define LED_H

/**
 * @brief  RGB 三色灯颜色枚举。
 * @note   P0 对应红色通道，P6 对应绿色通道，P9 对应蓝色通道。
 */
typedef enum {
    LED_COLOR_OFF = 0,    /* 三色全灭 */
    LED_COLOR_RED,        /* 仅 P0 红通道点亮 */
    LED_COLOR_GREEN,      /* 仅 P6 绿通道点亮 */
    LED_COLOR_BLUE,       /* 仅 P9 蓝通道点亮 */
    LED_COLOR_WHITE,      /* 三通道全亮，混出白光 */
} led_color_t;

/**
 * @brief  Initialize the P0/P6/P9 pins as GPIO output for RGB LED.
 * @note   Must be called before led_set_color()/led_flash().
 */
void led_init(void);

/**
 * @brief  Set the RGB LED to a specified color.
 * @param  [in] color  Color to show, see @ref led_color_t.
 */
void led_set_color(led_color_t color);

/**
 * @brief  Toggle the output level of P0/P6/P9 once.
 * @note   Kept for backward compatibility: flips all three channels.
 */
void led_flash(void);

/**
 * @brief  Cycle RED -> GREEN -> BLUE forever (blocking loop, run it in a task).
 */
void led_flash_forever(void);

#endif /* LED_H */
