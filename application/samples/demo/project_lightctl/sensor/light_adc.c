/**
 * 光敏传感器驱动实现（ADC 采样）
 *
 * 采样策略：
 *   1. 单次循环内连续采 ADC_SAMPLE_TIMES 次求平均 —— 抑制 ADC 的瞬时抖动；
 *   2. 跨调用再做一份 N 点滑动平均 —— 抑制环境光缓慢变化中的毛刺。
 *   两级平均后既能稳定读数，又不会对"挡光 / 照光"这类真实变化反应迟钝。
 */
#include <stdio.h>
#include "pinctrl.h"
#include "adc.h"
#include "adc_porting.h"
#include "common_def.h"
#include "soc_osal.h"
#include "light_adc.h"

/* 满量程电压（mV）：adc_port_read() 直接返回 mV 值，按 3.3V 供电估算 */
#define ADC_MAX_MV        3300
/* 单次读取内的采样次数 */
#define ADC_SAMPLE_TIMES  8
/* 滑动平均的窗口长度（必须是 2 的幂，便于用移位取平均） */
#define ADC_AVG_WINDOW    4

/* 滑动平均缓冲 */
static uint16_t g_win[ADC_AVG_WINDOW] = {0};
static uint8_t g_win_idx = 0;
static uint8_t g_win_cnt = 0;
static uint8_t g_inited = 0;

int light_adc_init(void)
{
    errcode_t ret;

    if (g_inited != 0U) {
        return 0;
    }

    /* ADC 初始化：使用内部时钟源 ADC_CLOCK_NONE 即可。
     * 注意 uapi_adc_init() 是幂等的，重复调用会直接返回 SUCC。 */
    ret = uapi_adc_init(ADC_CLOCK_NONE);
    if (ret != ERRCODE_SUCC) {
        printf("light adc: uapi_adc_init fail, ret = 0x%x\r\n", ret);
        return -1;
    }

    /* 打开通道并检查返回值：这一步会顺带把引脚切成模拟输入模式。
     * 如果通道号越界（WS63 只有 CH0~CH5），这里会返回 INVALID_PARAMETER。 */
    ret = uapi_adc_open_channel(LIGHT_ADC_CHANNEL);
    if (ret != ERRCODE_SUCC) {
        printf("light adc: open channel %d fail, ret = 0x%x (通道号必须 0~5)\r\n",
               LIGHT_ADC_CHANNEL, ret);
        return -1;
    }

    /* 打开 ADC 供电，否则部分通道读数为 0 */
    uapi_adc_power_en(AFE_GADC_MODE, true);

    g_win_idx = 0;
    g_win_cnt = 0;
    g_inited = 1U;

    printf("light adc init ok, channel = %d (P12)\r\n", LIGHT_ADC_CHANNEL);
    return 0;
}

int light_adc_read(light_adc_data_t *out)
{
    uint32_t sum = 0;
    uint32_t avg = 0;
    uint16_t voltage = 0;
    uint32_t i;
    errcode_t ret;

    if (out == NULL) {
        return -1;
    }
    if (g_inited == 0U) {
        return -1;
    }

    /* 1. 连续采样求平均 */
    for (i = 0; i < ADC_SAMPLE_TIMES; i++) {
        ret = adc_port_read(LIGHT_ADC_CHANNEL, &voltage);
        if (ret != ERRCODE_SUCC) {
            printf("light adc: read ch%d fail, ret = 0x%x\r\n", LIGHT_ADC_CHANNEL, ret);
            return -1;
        }
        sum += voltage;
    }

    /* 2. 写入滑动窗口 */
    g_win[g_win_idx] = (uint16_t)(sum / ADC_SAMPLE_TIMES);
    g_win_idx = (uint8_t)((g_win_idx + 1U) % ADC_AVG_WINDOW);
    if (g_win_cnt < ADC_AVG_WINDOW) {
        g_win_cnt++;
    }

    /* 3. 窗口内再求平均 */
    sum = 0;
    for (i = 0; i < g_win_cnt; i++) {
        sum += g_win[i];
    }
    avg = sum / g_win_cnt;

    /* 4. 越界钳位（保护后续百分比计算不被异常值带偏） */
    if (avg > ADC_MAX_MV) {
        avg = ADC_MAX_MV;
    }

    out->voltage_mv = (uint16_t)avg;
    out->percent = (uint8_t)(avg * 100U / ADC_MAX_MV);
    if (out->percent > 100U) {
        out->percent = 100U;
    }

    printf("light raw: ch%d -> %d mV, percent=%d\r\n",
           LIGHT_ADC_CHANNEL, out->voltage_mv, out->percent);
    return 0;
}
