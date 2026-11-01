/**
 * 光敏传感器驱动（ADC 采样）—— 对应"3 针光敏电阻模块"
 *
 * 硬件说明：
 *   本模块只有 3 个引脚（VCC / GND / AO），**没有数字量 DO 输出**，
 *   因此只能读模拟量 AO，必须走 ADC 通道，不能用 GPIO 数字读取。
 *   模块内部是 光敏电阻 + 分压电阻，环境越亮 AO 电压越高（常见接法）。
 *
 * ⚠ 关键：必须调用 uapi_adc_open_channel() 打开通道 + uapi_adc_power_en()
 *   打开 ADC 供电，否则部分通道读数为 0。
 */
#include <stdio.h>
#include "pinctrl.h"
#include "adc.h"
#include "adc_porting.h"
#include "common_def.h"
#include "soc_osal.h"
#include "light_sensor.h"

/*
 * 满量程电压（mV）。
 * adc_port_read() 直接返回 mV 值（见官方 adc_demo_inc.c 的 "voltage: %d mv" 打印）。
 * 这里按 3.3V 供电满量程估算，用于把电压归一化成 0~100 的百分比。
 */
#define ADC_MAX_MV        3300
/* 采样次数，取平均以减少抖动 */
#define ADC_SAMPLE_TIMES  8
/* 判定为"暗"的电压阈值（mV），按实际环境调整 */
#define LIGHT_DARK_MV     1000

int light_sensor_init(void)
{
    errcode_t ret;

    /* ADC 初始化：使用内部时钟源 ADC_CLOCK_NONE 即可。
     * uapi_adc_init() 是幂等的，重复调用会直接返回 SUCC。 */
    ret = uapi_adc_init(ADC_CLOCK_NONE);
    if (ret != ERRCODE_SUCC) {
        printf("adc init fail, ret = 0x%x\r\n", ret);
        return -1;
    }

    /* 打开通道并检查返回值：这一步会顺带把引脚切成模拟输入模式。 */
    ret = uapi_adc_open_channel(LIGHT_ADC_CHANNEL);
    if (ret != ERRCODE_SUCC) {
        printf("adc open channel %d fail, ret = 0x%x (通道号必须 0~5)\r\n",
               LIGHT_ADC_CHANNEL, ret);
        return -1;
    }

    /* 打开 ADC 供电，否则部分通道读数为 0。 */
    uapi_adc_power_en(AFE_GADC_MODE, true);

    printf("light sensor init ok, adc channel = %d\r\n", LIGHT_ADC_CHANNEL);
    return 0;
}

int light_sensor_read(light_data_t *data)
{
    uint32_t sum = 0;
    uint16_t voltage = 0;
    uint32_t i;
    errcode_t ret;

    if (data == NULL) {
        return -1;
    }

    /* 连续采样取平均：ADC 单次读数抖动较大 */
    for (i = 0; i < ADC_SAMPLE_TIMES; i++) {
        ret = adc_port_read(LIGHT_ADC_CHANNEL, &voltage);
        if (ret != ERRCODE_SUCC) {
            printf("adc read ch%d fail, ret = 0x%x\r\n", LIGHT_ADC_CHANNEL, ret);
            return -1;
        }
        sum += voltage;
        osal_msleep(5);
    }
    data->voltage_mv = (uint16_t)(sum / ADC_SAMPLE_TIMES);

    /* 调试打印：直接暴露每次采样的原始 mV，用于判断光敏是否真的有输出 */
    printf("light raw: ch%d -> %d mV\r\n", LIGHT_ADC_CHANNEL, data->voltage_mv);

    /* 归一化为 0~100 的亮度百分比 */
    if (data->voltage_mv >= ADC_MAX_MV) {
        data->percent = 100;
    } else {
        data->percent = (uint8_t)((uint32_t)data->voltage_mv * 100 / ADC_MAX_MV);
    }

    data->is_dark = (data->voltage_mv < LIGHT_DARK_MV) ? 1 : 0;
    return 0;
}
