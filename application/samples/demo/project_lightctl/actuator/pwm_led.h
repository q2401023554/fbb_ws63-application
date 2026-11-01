/**
 * PWM 亮度调节驱动（共阴 RGB 灯）
 *
 * 功能：用 3 路 WS63 硬件 PWM 驱动 RGB 灯模块的 R / G / B 三个阳极，
 *       三路占空比始终一致 → 合成白光，亮度 0~100 无级可调。
 *
 * ─── 为什么是 3 路而不是 1 路 ───
 *   LED 模块是「共阴 4 针」（R / G / B / 公共阴极）。只接 R 只能得到红光，
 *   把三个阳极都驱动起来才是正常的白光灯具。三路用同一个亮度值，
 *   对上层仍只是一个「亮度」参数，不构成额外功能。
 *
 * ─── 共阴接法的电平逻辑 ───
 *   公共阴极接 GND，引脚输出**高电平**点亮 → 占空比 = 高电平计数 / 周期计数。
 *   （若换成共阳模块，把下面 PWM_LED_ACTIVE_HIGH 改成 0 即可）
 *
 * ─── 引脚与 PWM 通道的绑定（硬件固定，不可自由选择）───
 *   依据官方《IO 复用关系表》（docs/zh-CN/software/IO复用关系.md）：
 *       PWM2 -> P2 / P10      PWM0 -> P0 / P8      PWM5 -> P5
 *   本工程取排针上互不冲突的三根：
 *       R -> P10 -> PWM2      G -> P8 -> PWM0      B -> P5 -> PWM5
 *
 * ─── 占空比换算 ───
 *   pwm_config_t 的 low_time / high_time 是「PWM 工作时钟的计数个数」，不是百分比。
 *   工作时钟由 uapi_pwm_get_frequency() 读出（WS63 为 40MHz），因此：
 *       周期计数 = 时钟频率 / 目标频率(1kHz) = 40000
 *       点亮计数 = 周期计数 × 亮度百分比
 */
#ifndef PWM_LED_H
#define PWM_LED_H

#include <stdint.h>

/* ---------- 三路引脚（可由 Kconfig 覆盖） ---------- */
#ifdef CONFIG_LIGHTCTL_LED_R_PIN
#define PWM_LED_PIN_R CONFIG_LIGHTCTL_LED_R_PIN
#else
#define PWM_LED_PIN_R 10
#endif

#ifdef CONFIG_LIGHTCTL_LED_G_PIN
#define PWM_LED_PIN_G CONFIG_LIGHTCTL_LED_G_PIN
#else
#define PWM_LED_PIN_G 8
#endif

#ifdef CONFIG_LIGHTCTL_LED_B_PIN
#define PWM_LED_PIN_B CONFIG_LIGHTCTL_LED_B_PIN
#else
#define PWM_LED_PIN_B 5
#endif

/* ---------- 三路 PWM 通道号（与上面引脚按硬件绑定，不要乱改） ---------- */
#ifdef CONFIG_LIGHTCTL_LED_R_CH
#define PWM_LED_CH_R CONFIG_LIGHTCTL_LED_R_CH
#else
#define PWM_LED_CH_R 2
#endif

#ifdef CONFIG_LIGHTCTL_LED_G_CH
#define PWM_LED_CH_G CONFIG_LIGHTCTL_LED_G_CH
#else
#define PWM_LED_CH_G 0
#endif

#ifdef CONFIG_LIGHTCTL_LED_B_CH
#define PWM_LED_CH_B CONFIG_LIGHTCTL_LED_B_CH
#else
#define PWM_LED_CH_B 5
#endif

/* ---------- PWM group 与调光频率 ---------- */
#ifdef CONFIG_LIGHTCTL_LED_GROUP
#define PWM_LED_GROUP CONFIG_LIGHTCTL_LED_GROUP
#else
#define PWM_LED_GROUP 0
#endif

/* 调光频率：1kHz 以上人眼察觉不到闪烁 */
#ifdef CONFIG_LIGHTCTL_LED_FREQ_HZ
#define PWM_LED_FREQ_HZ CONFIG_LIGHTCTL_LED_FREQ_HZ
#else
#define PWM_LED_FREQ_HZ 1000
#endif

/* 引脚复用模式：MODE 1 = PWM */
#define PWM_LED_PIN_MODE 1

/* 1 = 引脚输出高电平点亮（共阴）；0 = 输出低电平点亮（共阳） */
#define PWM_LED_ACTIVE_HIGH 1

/* 周期计数上限（驱动内部为 16bit 计数） */
#define PWM_LED_PERIOD_MAX 60000U

/**
 * @brief 初始化三路 PWM 并关灯
 * @return 0 成功，-1 失败
 */
int pwm_led_init(void);

/**
 * @brief 开 / 关灯（保持当前亮度）
 * @param on 1 = 开，0 = 关
 * @return 0 成功，-1 失败
 */
int pwm_led_set_on(uint8_t on);

/**
 * @brief 设置亮度 0~100
 * @param percent 亮度百分比，超过 100 会被截断
 * @return 0 成功，-1 失败
 */
int pwm_led_set_bright(uint8_t percent);

/**
 * @brief 读取当前灯的开关与亮度
 * @param on     输出：开关状态，可为 NULL
 * @param bright 输出：亮度百分比，可为 NULL
 */
void pwm_led_get_state(uint8_t *on, uint8_t *bright);

#endif
