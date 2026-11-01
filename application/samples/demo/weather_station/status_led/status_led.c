/**
 * 状态指示 RGB LED 驱动（外接 4 针模块，R/G/B 三路各占一个 GPIO）
 *
 * 引脚配置方式完全照搬本工程已验证可用的 RGB 例程
 * （application/samples/demo/LED/led.c），三步一个不少：
 *   1. uapi_pin_set_mode(引脚, HAL_PIO_FUNC_GPIO)       -> 引脚复用为普通 GPIO
 *   2. uapi_gpio_set_dir(引脚, GPIO_DIRECTION_OUTPUT)   -> 设为输出
 *   3. uapi_gpio_set_val(引脚, 电平)                     -> 输出高/低
 * 三步都检查返回值并打印，便于区分"代码没生效"和"硬件不通"。
 *
 * 颜色约定：
 *   绿色 = 心跳（每次成功获取/上报一次结果闪一下）
 *   红色 = 异常（WiFi / HTTP / MQTT 任一失败快闪 3 下）
 *   蓝色 = 开机自检时单独点亮，用来验证第三路接线
 *
 * 接线：R->P0、G->P5、B->P7，公共脚按共阴接 GND（共阳接 3.3V，
 *       并把 app_config.h 的 LED_RGB_COMMON_ANODE 改成 1）。详见 app_config.h。
 */
#include <stdio.h>
#include <stdbool.h>
#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"
#include "app_config.h"
#include "status_led.h"

/* 熄灭电平 = 点亮电平取反（共阴/共阳由 LED_RGB_ON_LEVEL 决定） */
#define LED_RGB_OFF_LEVEL   ((LED_RGB_ON_LEVEL == GPIO_LEVEL_HIGH) ? GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH)

/**
 * @brief 按 led.c 的方式配置单个引脚：复用为 GPIO + 输出 + 初始熄灭
 * @param pin 目标引脚
 * @return 0 全部步骤成功，-1 有步骤失败（失败步骤会打印到串口）
 */
static int status_led_pin_setup(pin_t pin)
{
    errcode_t ret;
    int result = 0;

    ret = uapi_pin_set_mode(pin, HAL_PIO_FUNC_GPIO);
    if (ret != ERRCODE_SUCC) {
        printf("[LED] pin %d set mode FAIL, ret=%d\r\n", pin, ret);
        result = -1;
    }

    ret = uapi_gpio_set_dir(pin, GPIO_DIRECTION_OUTPUT);
    if (ret != ERRCODE_SUCC) {
        printf("[LED] pin %d set dir FAIL, ret=%d\r\n", pin, ret);
        result = -1;
    }

    /* 先输出熄灭电平，避免上电瞬间误亮 */
    ret = uapi_gpio_set_val(pin, LED_RGB_OFF_LEVEL);
    if (ret != ERRCODE_SUCC) {
        printf("[LED] pin %d set val FAIL, ret=%d\r\n", pin, ret);
        result = -1;
    }

    return result;
}

/**
 * @brief 设置三路通道的电平（true = 该路点亮）
 *
 * 这是所有颜色效果的底层入口，等价于 led.c 里的 led_set_color()。
 */
static void status_led_rgb(bool r, bool g, bool b)
{
    uapi_gpio_set_val(LED_RGB_R_PIN, r ? LED_RGB_ON_LEVEL : LED_RGB_OFF_LEVEL);
    uapi_gpio_set_val(LED_RGB_G_PIN, g ? LED_RGB_ON_LEVEL : LED_RGB_OFF_LEVEL);
    uapi_gpio_set_val(LED_RGB_B_PIN, b ? LED_RGB_ON_LEVEL : LED_RGB_OFF_LEVEL);
}

/**
 * @brief 三色轮播 + 全亮自检，用于确认三路接线是否都通
 *
 * 顺序：红 -> 绿 -> 蓝 -> 全亮闪 N 次（N 由 LED_STATUS_SELFTEST_TIMES 决定）
 * 判读：
 *   只有某一路亮      -> 另外两路的线/针脚有问题
 *   三路全不亮        -> 公共脚没接（共阴需接 GND）或模块没供电，与代码无关
 *   三路都亮          -> 接线正确
 */
static void status_led_selftest(void)
{
    printf("[INIT] RGB self-test: R(P%d) -> G(P%d) -> B(P%d)\r\n",
           LED_RGB_R_PIN, LED_RGB_G_PIN, LED_RGB_B_PIN);

    /* 红 */
    status_led_rgb(true, false, false);
    printf("[INIT]   R on  (P%d)\r\n", LED_RGB_R_PIN);
    osal_msleep(LED_STATUS_SELFTEST_ON_MS);
    status_led_rgb(false, false, false);
    osal_msleep(LED_STATUS_SELFTEST_OFF_MS);

    /* 绿 */
    status_led_rgb(false, true, false);
    printf("[INIT]   G on  (P%d)\r\n", LED_RGB_G_PIN);
    osal_msleep(LED_STATUS_SELFTEST_ON_MS);
    status_led_rgb(false, false, false);
    osal_msleep(LED_STATUS_SELFTEST_OFF_MS);

    /* 蓝 */
    status_led_rgb(false, false, true);
    printf("[INIT]   B on  (P%d)\r\n", LED_RGB_B_PIN);
    osal_msleep(LED_STATUS_SELFTEST_ON_MS);
    status_led_rgb(false, false, false);
    osal_msleep(LED_STATUS_SELFTEST_OFF_MS);

    /* 三路同亮（白光），闪 N 次 */
    printf("[INIT]   RGB all on, blink %d times\r\n", LED_STATUS_SELFTEST_TIMES);
    status_led_blink((uint8_t)LED_STATUS_SELFTEST_TIMES,
                     LED_STATUS_SELFTEST_ON_MS,
                     LED_STATUS_SELFTEST_OFF_MS);

    status_led_rgb(false, false, false);
}

void status_led_init(void)
{
    int ok = 0;

    /* 三路逐个配置，任何一路失败只单独报出来，不影响其余两路继续初始化 */
    if (status_led_pin_setup(LED_RGB_R_PIN) != 0) {
        ok = -1;
    }
    if (status_led_pin_setup(LED_RGB_G_PIN) != 0) {
        ok = -1;
    }
    if (status_led_pin_setup(LED_RGB_B_PIN) != 0) {
        ok = -1;
    }

    printf("[INIT] Status RGB LED R=P%d G=P%d B=P%d, common=%s, setup %s\r\n",
           LED_RGB_R_PIN, LED_RGB_G_PIN, LED_RGB_B_PIN,
           (LED_RGB_COMMON_ANODE > 0) ? "ANODE(3V3)" : "CATHODE(GND)",
           (ok == 0) ? "OK" : "FAIL");

    /* 初始全灭 */
    status_led_rgb(false, false, false);

#if (LED_STATUS_SELFTEST_TIMES > 0)
    status_led_selftest();
#endif

#if (LED_STATUS_SWEEP_AT_BOOT > 0)
    printf("[INIT] sweep at boot enabled\r\n");
    status_led_sweep_test();
#endif
}

void status_led_set(bool on)
{
    /* 全亮 / 全灭（保留给外部按需调用） */
    status_led_rgb(on, on, on);
}

void status_led_blink(uint8_t times, uint32_t on_ms, uint32_t off_ms)
{
    for (uint8_t i = 0; i < times; i++) {
        status_led_rgb(true, true, true);
        osal_msleep(on_ms);
        status_led_rgb(false, false, false);
        if (i < (uint8_t)(times - 1)) {
            osal_msleep(off_ms);
        }
    }
}

void status_led_heartbeat(void)
{
    /* 每次成功获取/上报一次结果：绿灯闪 1 下（绿=正常，符合项目颜色约定）*/
    status_led_rgb(false, true, false);
    osal_msleep(LED_STATUS_REPORT_ON_MS);
    status_led_rgb(false, false, false);
}

void status_led_error(void)
{
    /* 任一环节失败：红灯快闪 3 下（红=警示）*/
    for (uint8_t i = 0; i < 3; i++) {
        status_led_rgb(true, false, false);
        osal_msleep(LED_STATUS_ERROR_ON_MS);
        status_led_rgb(false, false, false);
        if (i < 2) {
            osal_msleep(LED_STATUS_ERROR_OFF_MS);
        }
    }
}

/**
 * @brief 扫脚测试（排障用，串口发 LEDTEST 触发）
 *
 * 分两段：
 *   第一段「逐路验证」——红、绿、蓝各亮 1 秒，确认三路接线；
 *   第二段「逐脚验证」——依次点亮 P0/P1/P3/P4/P5/P7/P8/P11/P12 各 1 秒，
 *                        把那根信号线换着孔插时，用来找哪个针脚真的有输出。
 *
 * 注意：P2/P9 是数码管插槽 selector（P9 必须恒为 1）、P10 是 DHT11、
 *       P13/P14 是数码管、P17/P18 是串口，这些脚不参与扫脚，避免打断正常运行。
 */
void status_led_sweep_test(void)
{
    static const pin_t sweep_pins[] = {
        GPIO_00, GPIO_01, GPIO_03, GPIO_04, GPIO_05, GPIO_07, GPIO_08, GPIO_11, GPIO_12
    };
    const uint32_t count = (uint32_t)(sizeof(sweep_pins) / sizeof(sweep_pins[0]));
    uint32_t i;

    /* 第一段：逐路验证（同时点亮三路，看哪一路有效果）*/
    printf("[LEDTEST] === stage 1: per-channel ===\r\n");
    printf("[LEDTEST] R only (P%d)\r\n", LED_RGB_R_PIN);
    status_led_rgb(true, false, false);
    osal_msleep(1000);
    printf("[LEDTEST] G only (P%d)\r\n", LED_RGB_G_PIN);
    status_led_rgb(false, true, false);
    osal_msleep(1000);
    printf("[LEDTEST] B only (P%d)\r\n", LED_RGB_B_PIN);
    status_led_rgb(false, false, true);
    osal_msleep(1000);
    status_led_rgb(false, false, false);
    osal_msleep(300);

    /* 第二段：逐脚验证（把信号线换孔时用）*/
    printf("[LEDTEST] === stage 2: per-pin (1s each) ===\r\n");
    for (i = 0; i < count; i++) {
        pin_t pin = sweep_pins[i];

        (void)uapi_pin_set_mode(pin, HAL_PIO_FUNC_GPIO);
        (void)uapi_gpio_set_dir(pin, GPIO_DIRECTION_OUTPUT);
        (void)uapi_gpio_set_val(pin, LED_RGB_ON_LEVEL);

        printf("[LEDTEST] GPIO_%d ON  <-- is any channel on now?\r\n", pin);
        osal_msleep(1000);
        (void)uapi_gpio_set_val(pin, LED_RGB_OFF_LEVEL);
        osal_msleep(300);
    }
    printf("[LEDTEST] sweep done, restore RGB pins\r\n");

    /* 扫完后恢复三路配置 */
    (void)status_led_pin_setup(LED_RGB_R_PIN);
    (void)status_led_pin_setup(LED_RGB_G_PIN);
    (void)status_led_pin_setup(LED_RGB_B_PIN);
}
