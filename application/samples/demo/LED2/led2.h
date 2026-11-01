/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 *
 * Description: LED2 (RGB, P0/P6/P9) driver interface header. \n
 *
 * History: \n
 * 2026-09-15, Create file. \n
 * 2026-09-16, Add P3 (green channel) and color control interface. \n
 * 2026-09-16, Change green channel from P3 to P6. \n
 * 2026-09-16, Rework to library: flash start/stop controlled by caller (e.g. UART cmd). \n
 */

#ifndef LED2_H
#define LED2_H

/**
 * @brief  RGB 三色灯颜色枚举。
 * @note   P0 对应红色通道，P6 对应绿色通道，P9 对应蓝色通道。
 */
typedef enum {
    LED2_COLOR_OFF = 0,    /* 三色全灭 */
    LED2_COLOR_RED,        /* 仅 P0 红通道点亮 */
    LED2_COLOR_GREEN,      /* 仅 P6 绿通道点亮 */
    LED2_COLOR_BLUE,       /* 仅 P9 蓝通道点亮 */
    LED2_COLOR_WHITE,      /* 三通道全亮，混出白光 */
} led2_color_t;

/**
 * @brief  Initialize the P0/P6/P9 pins as GPIO output for RGB LED.
 * @note   Must be called before led2_set_color()/led2_flash().
 *         led2_flash_start() calls this internally.
 */
void led2_init(void);

/**
 * @brief  Set the RGB LED to a specified color.
 * @param  [in] color  Color to show, see @ref led2_color_t.
 */
void led2_set_color(led2_color_t color);

/**
 * @brief  Toggle the output level of P0/P6/P9 once.
 * @note   Kept for backward compatibility: flips all three channels.
 */
void led2_flash(void);

/**
 * @brief  Start the RGB cycling task (RED -> GREEN -> BLUE, non-blocking).
 * @note   Idempotent: does nothing if already flashing.
 */
void led2_flash_start(void);

/**
 * @brief  Stop the RGB cycling task and turn the LED off.
 * @note   Idempotent: does nothing if not flashing.
 */
void led2_flash_stop(void);

#endif /* LED2_H */
