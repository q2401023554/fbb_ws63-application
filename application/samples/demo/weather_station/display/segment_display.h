#ifndef SEGMENT_DISPLAY_H
#define SEGMENT_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 初始化数码管（选择器引脚 + TM1637 引脚 + 点亮）
 */
void segment_display_init(void);

/**
 * @brief 显示 0~9999 整数（不带小数点，用于"网络天气"数值）
 */
void segment_display_set_number(uint16_t number);

/**
 * @brief 显示 0~9999 整数，并可在第 2 位数字上点亮小数点
 * @param number 0~9999
 * @param show_dp true = 点亮第 2 位小数点（本工程约定：带小数点 = 本地采集数据）
 *
 * 4 位数码管没有单位，用"小数点"区分数据来源：
 *   带 dp -> 本地 DHT11 数据；不带 dp -> 网络天气数据。
 */
void segment_display_set_number_dp(uint16_t number, bool show_dp);

void segment_display_clear(void);

#endif
