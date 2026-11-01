/**
 * TM1637 四位数码管驱动（软件模拟两线时序）
 * 来源：南向设备开发考核 smart_light_monitor 工程（已烧录验证），
 *       在其基础上增加"小数点段"支持，用于区分本地/网络数据。
 * 引脚：SDA=GPIO13  SCL=GPIO14  selector GPIO2=0/GPIO9=1
 */
#include "segment_display.h"
#include "app_config.h"
#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"
#include "tcxo.h"
#include <stdio.h>

#define TM1637_BRIGHTNESS 0x07

/* 段码表 bit0~bit7 = a b c d e f g dp，共阴数码管 0~9 */
static const uint8_t g_seg_table[10] = {
    0x3f, /* 0 */
    0x06, /* 1 */
    0x5b, /* 2 */
    0x4f, /* 3 */
    0x66, /* 4 */
    0x6d, /* 5 */
    0x7d, /* 6 */
    0x07, /* 7 */
    0x7f, /* 8 */
    0x6f  /* 9 */
};

static uint16_t g_last_number = 0xffff;   /* 上次显示值，相同值不重复刷新 */
static bool g_last_dp = false;

/* 软件延时约 5us。
 * 必须用 TCXO 硬件计数器延时（uapi_tcxo_delay_us），精确且不受编译优化影响。
 * 旧实现为 50 次 nop 空转，实测仅约 0.2us，低于 TM1637 时钟脉宽下限（400ns 级），
 * 会导致芯片完全收不到数据、数码管全黑（实验六已定位并验证的根因）。 */
static void delay_5us(void)
{
    (void)uapi_tcxo_delay_us(5);
}

static void sda_high(void) { uapi_gpio_set_val(TM1637_SDA_PIN, GPIO_LEVEL_HIGH); }
static void sda_low(void)  { uapi_gpio_set_val(TM1637_SDA_PIN, GPIO_LEVEL_LOW); }
static void scl_high(void) { uapi_gpio_set_val(TM1637_SCL_PIN, GPIO_LEVEL_HIGH); }
static void scl_low(void)  { uapi_gpio_set_val(TM1637_SCL_PIN, GPIO_LEVEL_LOW); }

/* 起始信号：SCL 高电平期间 SDA 由高->低 */
static void tm1637_start(void)
{
    sda_high();
    scl_high();
    delay_5us();
    sda_low();
    delay_5us();
    scl_low();
    delay_5us();
}

/* 停止信号：SCL 高电平期间 SDA 由低->高 */
static void tm1637_stop(void)
{
    scl_low();
    delay_5us();
    sda_low();
    delay_5us();
    scl_high();
    delay_5us();
    sda_high();
    delay_5us();
}

/* 写入一个字节（低位先发），最后读 ACK 应答位 */
static void tm1637_write_byte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++) {
        scl_low();
        delay_5us();
        if ((data & 0x01) != 0) {
            sda_high();
        } else {
            sda_low();
        }
        delay_5us();
        scl_high();
        delay_5us();
        data >>= 1;
    }
    /* 第 9 个时钟：释放 SDA，读取从机应答 */
    scl_low();
    delay_5us();
    uapi_gpio_set_dir(TM1637_SDA_PIN, GPIO_DIRECTION_INPUT);
    delay_5us();
    scl_high();
    delay_5us();
    scl_low();
    uapi_gpio_set_dir(TM1637_SDA_PIN, GPIO_DIRECTION_OUTPUT);
    delay_5us();
}

static void tm1637_write_cmd(uint8_t cmd)
{
    tm1637_start();
    tm1637_write_byte(cmd);
    tm1637_stop();
}

static void tm1637_set_brightness(uint8_t brightness)
{
    tm1637_write_cmd(0x88 | (brightness & 0x07));
}

/* 一次写入 4 位数字（d1 为最高位，d4 为最低位），dp_mask 对应位为 1 时该位点亮小数点 */
static void tm1637_display_digits(uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4, uint8_t dp_mask)
{
    uint8_t data[4] = {
        (uint8_t)(g_seg_table[d1 % 10] | ((dp_mask & 0x08) ? 0x80 : 0x00)),
        (uint8_t)(g_seg_table[d2 % 10] | ((dp_mask & 0x04) ? 0x80 : 0x00)),
        (uint8_t)(g_seg_table[d3 % 10] | ((dp_mask & 0x02) ? 0x80 : 0x00)),
        (uint8_t)(g_seg_table[d4 % 10] | ((dp_mask & 0x01) ? 0x80 : 0x00))
    };

    /* 写显示模式：固定地址自动加 1 */
    tm1637_start();
    tm1637_write_byte(0x40);
    tm1637_stop();

    /* 从地址 0xC0 开始写 4 个段码数据 */
    tm1637_start();
    tm1637_write_byte(0xc0);
    for (uint8_t i = 0; i < 4; i++) {
        tm1637_write_byte(data[i]);
    }
    tm1637_stop();

    tm1637_set_brightness(TM1637_BRIGHTNESS);
}

/* 板载插槽模式选择：GPIO2=0、GPIO9=1 表示数码管模式 */
static void selector_init(void)
{
    uapi_pin_set_mode(DISPLAY_SELECTOR_PIN2, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(DISPLAY_SELECTOR_PIN2, GPIO_DIRECTION_OUTPUT);

    uapi_pin_set_mode(DISPLAY_SELECTOR_PIN9, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(DISPLAY_SELECTOR_PIN9, GPIO_DIRECTION_OUTPUT);

    uapi_gpio_set_val(DISPLAY_SELECTOR_PIN2, GPIO_LEVEL_LOW);
    uapi_gpio_set_val(DISPLAY_SELECTOR_PIN9, GPIO_LEVEL_HIGH);

    printf("[INIT] Selector set: GPIO2=0 GPIO9=1 digital tube mode\r\n");
}

void segment_display_init(void)
{
    selector_init();

    uapi_pin_set_mode(TM1637_SDA_PIN, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(TM1637_SDA_PIN, GPIO_DIRECTION_OUTPUT);
    sda_high();

    uapi_pin_set_mode(TM1637_SCL_PIN, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(TM1637_SCL_PIN, GPIO_DIRECTION_OUTPUT);
    scl_high();

    osal_msleep(10);
    tm1637_write_cmd(0x88 | TM1637_BRIGHTNESS);   /* 开显示 */
    segment_display_set_number(0);

    printf("[INIT] TM1637 initialized: SDA=GPIO_%d SCL=GPIO_%d\r\n",
        TM1637_SDA_PIN, TM1637_SCL_PIN);
}

void segment_display_clear(void)
{
    tm1637_start();
    tm1637_write_byte(0x40);
    tm1637_stop();

    tm1637_start();
    tm1637_write_byte(0xc0);
    for (uint8_t i = 0; i < 4; i++) {
        tm1637_write_byte(0x00);
    }
    tm1637_stop();

    tm1637_set_brightness(TM1637_BRIGHTNESS);
    g_last_number = 0xffff;
}

static void segment_display_write(uint16_t number, bool show_dp)
{
    if (number > 9999) {
        number = 9999;
    }
    /* 数值和小数点状态都相同才跳过刷新 */
    if (number == g_last_number && show_dp == g_last_dp) {
        return;
    }
    g_last_number = number;
    g_last_dp = show_dp;

    /* show_dp = true 时小数点点亮在第 2 位（从左数），如 26. 形式 */
    tm1637_display_digits(
        (number / 1000) % 10,
        (number / 100) % 10,
        (number / 10) % 10,
        number % 10,
        show_dp ? 0x04 : 0x00);
}

void segment_display_set_number(uint16_t number)
{
    segment_display_write(number, false);
}

void segment_display_set_number_dp(uint16_t number, bool show_dp)
{
    segment_display_write(number, show_dp);
}
