/**
 * PWM 亮度调节驱动实现
 *
 * 依据官方 PWM 云 API（include/driver/pwm.h）：
 *   V151 版本需要先 uapi_pwm_open() 打开各路，再用 uapi_pwm_set_group() 归组，
 *   最后 uapi_pwm_start_group() 统一启动；运行中改占空比用 uapi_pwm_update_cfg()。
 *   pwm_config_t.low_time / high_time 是「工作时钟的计数个数」，不是百分比。
 */
#include <stdio.h>
#include <stdbool.h>
#include "pinctrl.h"
#include "pwm.h"
#include "common_def.h"
#include "pwm_led.h"

/* 三路的引脚与通道，统一按 R/G/B 顺序索引，方便用同一个循环刷新 */
static const uint8_t g_pwm_pins[3] = { PWM_LED_PIN_R, PWM_LED_PIN_G, PWM_LED_PIN_B };
static const uint8_t g_pwm_chs[3]  = { PWM_LED_CH_R,  PWM_LED_CH_G,  PWM_LED_CH_B  };

/* ---------------- 灯的状态 ---------------- */
static uint8_t g_on = 0;                /* 开关 */
static uint8_t g_bright = 80;           /* 亮度 0~100 */
static uint8_t g_inited = 0;
static uint32_t g_period = 40000U;      /* 周期计数，init 时按实际工作时钟重算 */

/**
 * @brief 把「点亮百分比」写成 3 路的 PWM 占空比
 * @param on_percent 点亮百分比 0~100
 * @return 0 成功，-1 失败
 */
static int pwm_led_apply(uint32_t on_percent)
{
    pwm_config_t cfg;
    uint32_t on_cnt;
    uint32_t high;
    uint8_t i;

    if (on_percent > 100U) {
        on_percent = 100U;
    }
    on_cnt = (uint32_t)(g_period * on_percent / 100U);

#if (PWM_LED_ACTIVE_HIGH != 0)
    high = on_cnt;               /* 共阴：输出高电平点亮 */
#else
    high = g_period - on_cnt;    /* 共阳：输出低电平点亮，高低电平互换 */
#endif

    cfg.low_time = g_period - high;
    cfg.high_time = high;
    cfg.offset_time = 0;
    cfg.cycles = 0xFF;
    cfg.repeat = true;

    for (i = 0; i < 3U; i++) {
        if (uapi_pwm_update_cfg(g_pwm_chs[i], &cfg) != ERRCODE_SUCC) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief 按当前 on / bright 刷新三路占空比
 */
static int pwm_led_refresh(void)
{
    if (g_inited == 0U) {
        return -1;
    }
    return pwm_led_apply((g_on != 0U) ? (uint32_t)g_bright : 0U);
}

int pwm_led_init(void)
{
    uint8_t channel_set[3];
    pwm_config_t cfg;
    uint32_t freq;
    uint8_t i;

    if (g_inited != 0U) {
        return 0;
    }

    /* 1. 三路引脚切到 PWM 复用（MODE 1） */
    for (i = 0; i < 3U; i++) {
        if (uapi_pin_set_mode(g_pwm_pins[i], (pin_mode_t)PWM_LED_PIN_MODE) != ERRCODE_SUCC) {
            printf("pwm led: set pin P%d to PWM mode failed\r\n", g_pwm_pins[i]);
            return -1;
        }
    }

    /* 2. 初始化 PWM 驱动 */
    if (uapi_pwm_init() != ERRCODE_SUCC) {
        printf("pwm led: uapi_pwm_init failed\r\n");
        return -1;
    }

    /* 3. 读 PWM 工作时钟，反算周期计数（WS63 为 40MHz → 40000） */
    freq = uapi_pwm_get_frequency(g_pwm_chs[0]);
    if (freq == 0U) {
        freq = 40000000U;
    }
    g_period = freq / (uint32_t)PWM_LED_FREQ_HZ;
    if ((g_period == 0U) || (g_period > PWM_LED_PERIOD_MAX)) {
        g_period = PWM_LED_PERIOD_MAX;
    }

    /* 4. 三路先按「全灭」打开 */
    cfg.low_time = g_period;
    cfg.high_time = 0;
    cfg.offset_time = 0;
    cfg.cycles = 0xFF;
    cfg.repeat = true;

    for (i = 0; i < 3U; i++) {
        if (uapi_pwm_open(g_pwm_chs[i], &cfg) != ERRCODE_SUCC) {
            printf("pwm led: open channel %d failed\r\n", g_pwm_chs[i]);
            return -1;
        }
        channel_set[i] = g_pwm_chs[i];
    }

    /* 5. 三路归入同一 group，保证相位同步（否则调光时会闪） */
    if (uapi_pwm_set_group(PWM_LED_GROUP, channel_set, 3U) != ERRCODE_SUCC) {
        printf("pwm led: uapi_pwm_set_group failed\r\n");
        return -1;
    }
    if (uapi_pwm_start_group(PWM_LED_GROUP) != ERRCODE_SUCC) {
        printf("pwm led: uapi_pwm_start_group failed\r\n");
        return -1;
    }

    g_inited = 1U;

    printf("pwm led init ok, R=P%d/PWM%d G=P%d/PWM%d B=P%d/PWM%d, %uHz, period=%u\r\n",
           PWM_LED_PIN_R, PWM_LED_CH_R, PWM_LED_PIN_G, PWM_LED_CH_G,
           PWM_LED_PIN_B, PWM_LED_CH_B,
           (unsigned int)(freq / g_period), (unsigned int)g_period);

    return pwm_led_refresh();
}

int pwm_led_set_on(uint8_t on)
{
    g_on = (on != 0U) ? 1U : 0U;
    return pwm_led_refresh();
}

int pwm_led_set_bright(uint8_t percent)
{
    if (percent > 100U) {
        percent = 100U;
    }
    g_bright = percent;
    return pwm_led_refresh();
}

void pwm_led_get_state(uint8_t *on, uint8_t *bright)
{
    if (on != NULL) {
        *on = g_on;
    }
    if (bright != NULL) {
        *bright = g_bright;
    }
}
