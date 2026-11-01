/**
 * 光敏传感器驱动（ADC 采样）—— 对应「3 针光敏电阻模块」
 *
 * 硬件说明：
 *   本模块只有 3 个引脚（VCC / GND / AO），没有数字量 DO 输出，
 *   因此只能读模拟量 AO，必须走 ADC 通道，不能用 GPIO 数字读取。
 *   模块内部是 光敏电阻 + 分压电阻，环境越亮 AO 电压越高（常见接法）。
 *
 * ⚠ 重要：WS63 的 ADC 通道与物理引脚是 HAL 里写死的绑定关系
 *   （见 hal_adc_v154.c 的 hal_adc_channel_trans_to_gpio_get），
 *   两者不能各自独立选择：
 *
 *     CH0 -> P7     CH1 -> P8     CH2 -> P9
 *     CH3 -> P10    CH4 -> P11(本板未引出)    CH5 -> P12
 *
 *   所以光敏模块的 AO 只能接在 P7/P8/P9/P10/P12 之一，
 *   本工程使用 P12（通道 5），与三路 PWM 的 P8/P10 不冲突。
 */
#ifndef LIGHT_ADC_H
#define LIGHT_ADC_H

#include <stdint.h>

#ifdef CONFIG_LIGHTCTL_LIGHT_ADC_CHANNEL
#define LIGHT_ADC_CHANNEL CONFIG_LIGHTCTL_LIGHT_ADC_CHANNEL
#else
#define LIGHT_ADC_CHANNEL 5
#endif

/* 光敏采集结果 */
typedef struct {
    uint16_t voltage_mv;   /* ADC 原始电压，单位 mV */
    uint8_t  percent;      /* 归一化环境光百分比 0~100 */
} light_adc_data_t;

/**
 * @brief 初始化 ADC（用于光敏电阻采样）
 * @return 0 成功，-1 失败
 */
int light_adc_init(void);

/**
 * @brief 读取一次环境光强度（内部含多次采样 + 滑动平均）
 * @param out 输出结果，不可为 NULL
 * @return 0 成功，-1 失败
 */
int light_adc_read(light_adc_data_t *out);

#endif
