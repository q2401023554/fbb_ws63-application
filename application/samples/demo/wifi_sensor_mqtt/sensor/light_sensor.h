#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

#include <stdint.h>

/*
 * 光敏传感器使用的 ADC 通道。
 *
 * ⚠ 重要：WS63 的 ADC 通道与物理引脚是 HAL 里写死的绑定关系
 *   （见 hal_adc_v154.c 的 hal_adc_channel_trans_to_gpio_get）：
 *     CH0 -> P7     CH1 -> P8     CH2 -> P9
 *     CH3 -> P10    CH4 -> P11(本板未引出)    CH5 -> P12
 *
 *   所以光敏模块的 AO 只能接在 P7/P8/P9/P10/P12 之一。
 *   本工程光敏 AO 接 P12，故通道取 5。
 */
#define LIGHT_ADC_CHANNEL 5

/* 光敏采样结果 */
typedef struct {
    uint16_t voltage_mv;   /* ADC 原始电压，单位 mV */
    uint8_t  percent;      /* 归一化亮度百分比 0~100 */
    uint8_t  is_dark;      /* 1 = 环境偏暗，0 = 环境偏亮 */
} light_data_t;

/**
 * @brief 初始化 ADC（用于光敏电阻采样）
 * @return 0 成功，-1 失败
 */
int light_sensor_init(void);

/**
 * @brief 读取一次光照强度
 * @param data 输出结果
 * @return 0 成功，-1 失败
 *
 * 注意：模块只有 AO 模拟输出（无 DO 数字输出），必须接 ADC 通道。
 */
int light_sensor_read(light_data_t *data);

#endif
