/**
 * TM1637 四位数码管驱动（软件模拟时序）
 * 来源：南向设备开发考核 smart_light_monitor 工程（已烧录验证）
 * 引脚：SDA=GPIO13  SCL=GPIO14  selector GPIO2=0/GPIO9=1
 */
#include "segment_display.h"
#include "app_config.h"
#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"
#include "tcxo.h"

#define TM1637_BRIGHTNESS 0x07

/* 共阴数码管 0~9 段码（a b c d e f g dp 从低到高） */
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

static uint16_t g_last_number = 0xffff;

/* TM1637 时序半周期延时：要求 > 2us，这里取 5us（用 TCXO 硬件计数器，精确不受编译优化影响）。
 * 注意：旧实现为 50 次 nop 空转，实际仅约 0.2us，低于 TM1637 时钟脉宽下限（400ns 级），
 * 会导致芯片完全收不到数据（与已验证可用的同学版代码的差异点即在此）。 */
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

/* 写入一个字节（低位先发），最后读 ACK 应答位。
 * 返回 0 = TM1637 已应答（数据链路正常）；1 = 无应答（引脚/插槽/供电有问题） */
static uint8_t tm1637_write_byte(uint8_t data)
{
    uint8_t ack = 1;

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
    /* 第 9 个时钟：释放 SDA，读取从机应答（TM1637 应答时会把 DIO 拉低） */
    scl_low();
    delay_5us();
    uapi_gpio_set_dir(TM1637_SDA_PIN, GPIO_DIRECTION_INPUT);
    delay_5us();
    scl_high();
    delay_5us();
    ack = (uapi_gpio_get_val(TM1637_SDA_PIN) == GPIO_LEVEL_HIGH) ? 1u : 0u;
    scl_low();
    uapi_gpio_set_dir(TM1637_SDA_PIN, GPIO_DIRECTION_OUTPUT);
    delay_5us();
    return ack;
}

static uint8_t tm1637_write_cmd(uint8_t cmd)
{
    uint8_t ack;

    tm1637_start();
    ack = tm1637_write_byte(cmd);
    tm1637_stop();
    return ack;
}

static uint8_t tm1637_set_brightness(uint8_t brightness)
{
    return tm1637_write_cmd(0x88 | (brightness & 0x07));
}

/* 开关显示：on=1 点亮（8/16 级亮度取 0x07），on=0 熄灭；返回该命令的 ACK */
static uint8_t tm1637_switch(uint8_t on)
{
    if (on != 0) {
        return tm1637_write_cmd(0x88 | TM1637_BRIGHTNESS);
    }
    return tm1637_write_cmd(0x80);
}

/* 一次写入 4 位数字（d1 为最高位，d4 为最低位），并把各阶段 ACK 结果打印到串口 */
static void tm1637_display_digits(uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4)
{
    uint8_t data[4] = {
        g_seg_table[d1 % 10],
        g_seg_table[d2 % 10],
        g_seg_table[d3 % 10],
        g_seg_table[d4 % 10]
    };
    uint8_t ack_cmd;
    uint8_t ack_addr;
    uint8_t ack_data;

    /* 写显示模式：固定地址自动加 1 */
    ack_cmd = tm1637_write_cmd(0x40);

    /* 从地址 0xC0 开始写 4 个段码数据 */
    tm1637_start();
    ack_addr = tm1637_write_byte(0xc0);
    ack_data = 0;
    for (uint8_t i = 0; i < 4; i++) {
        ack_data |= tm1637_write_byte(data[i]);
    }
    tm1637_stop();

    tm1637_set_brightness(TM1637_BRIGHTNESS);

    /* 诊断输出：全 0 = 芯片应答正常；出现 1 = 总线无应答，查接线/插槽/供电 */
    printf("[SEG] ACK cmd=%u addr=%u data=%u (0=应答正常 1=无应答)\r\n",
           ack_cmd, ack_addr, ack_data);
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

/* 清屏：4 位全部写 0 */
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

void segment_display_init(void)
{
    uint8_t ack_on;

    selector_init();

    uapi_pin_set_mode(TM1637_SDA_PIN, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(TM1637_SDA_PIN, GPIO_DIRECTION_OUTPUT);
    /* SDA 释放（输入）期间靠内部上拉保持高电平，保证 ACK 位读值可靠 */
    (void)uapi_pin_set_pull(TM1637_SDA_PIN, PIN_PULL_TYPE_UP);
    sda_high();

    uapi_pin_set_mode(TM1637_SCL_PIN, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(TM1637_SCL_PIN, GPIO_DIRECTION_OUTPUT);
    scl_high();

    osal_msleep(10);
    ack_on = tm1637_switch(1);
    segment_display_set_number(0);

    printf("[INIT] TM1637 initialized: SDA=GPIO_%d SCL=GPIO_%d\r\n",
        TM1637_SDA_PIN, TM1637_SCL_PIN);
    printf("[INIT] display-on cmd ACK=%u (0=芯片应答正常 1=无应答-查插槽/接线/供电)\r\n",
        ack_on);
}

/* 显示 0~9999；超过 9999 截断为 9999；相同值不重复刷新 */
void segment_display_set_number(uint16_t number)
{
    if (number > 9999) {
        number = 9999;
    }
    if (number == g_last_number) {
        return;
    }
    g_last_number = number;
    tm1637_display_digits(
        (number / 1000) % 10,
        (number / 100) % 10,
        (number / 10) % 10,
        number % 10);
}

void segment_display_refresh_once(void)
{
}
