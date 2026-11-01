/**
 * TM1637 四位数码管驱动实现
 *
 * 两线串行接口（CLK + DIO），软件模拟时序。
 * 关键点：DIO 是双向线。写数据时是输出；读 ACK 时要切成输入并开上拉。
 * TM1637 最高时钟频率约 250kHz，因此每次电平变化之间加约 5us 延时。
 *
 * 时序参考：起始条件 = CLK 高时 DIO 下降沿；停止条件 = CLK 高时 DIO 上升沿。
 * 数据在 CLK 上升沿被芯片采样，字节按 LSB 先出。
 *
 * 接线：CLK -> P14，DIO -> P13（见 tm1637.h）
 */
#include <stdio.h>
#include <string.h>
#include "pinctrl.h"
#include "gpio.h"
#include "soc_osal.h"
#include "common_def.h"
#include "tm1637.h"

/* ---------- 位延时 ----------
 * TM1637 规定最小脉宽 400ns，典型时钟 250kHz（周期 4us）。
 * uapi_tcxo_delay_us() 在此场景下依赖 TCXO，未初始化时会静默不延时，
 * 因此这里用忙等循环保证时序稳定（每次约 5us @ 160MHz）。
 */
static void tm1637_delay(void)
{
    uint32_t i;
    for (i = 0; i < 400; i++) {
        __asm__ volatile ("nop");
    }
}

/* ---------- 方向控制 ---------- */
static void tm1637_dio_output(void)
{
    uapi_gpio_set_dir(TM1637_DIO_PIN, GPIO_DIRECTION_OUTPUT);
}

static void tm1637_dio_input(void)
{
    uapi_gpio_set_dir(TM1637_DIO_PIN, GPIO_DIRECTION_INPUT);
}

/* ---------- 电平控制 ---------- */
static void tm1637_clk_high(void)
{
    uapi_gpio_set_val(TM1637_CLK_PIN, GPIO_LEVEL_HIGH);
}

static void tm1637_clk_low(void)
{
    uapi_gpio_set_val(TM1637_CLK_PIN, GPIO_LEVEL_LOW);
}

static void tm1637_dio_high(void)
{
    uapi_gpio_set_val(TM1637_DIO_PIN, GPIO_LEVEL_HIGH);
}

static void tm1637_dio_low(void)
{
    uapi_gpio_set_val(TM1637_DIO_PIN, GPIO_LEVEL_LOW);
}

static uint8_t tm1637_dio_read(void)
{
    return (uapi_gpio_get_val(TM1637_DIO_PIN) == GPIO_LEVEL_HIGH) ? 1 : 0;
}

/* ---------- 协议层 ---------- */

/* 起始条件：CLK 为高时，DIO 由高变低 */
static void tm1637_start(void)
{
    tm1637_dio_output();
    tm1637_clk_high();
    tm1637_dio_high();
    tm1637_delay();
    tm1637_dio_low();
    tm1637_delay();
    tm1637_clk_low();       /* 准备传输第一个数据位 */
    tm1637_delay();
}

/* 停止条件：CLK 为高时，DIO 由低变高 */
static void tm1637_stop(void)
{
    tm1637_dio_output();
    tm1637_clk_low();
    tm1637_delay();
    tm1637_dio_low();
    tm1637_delay();
    tm1637_clk_high();
    tm1637_delay();
    tm1637_dio_high();
    tm1637_delay();
}

/* 写一个字节，LSB 先出；返回 0 表示收到 ACK，-1 表示超时无应答 */
static int tm1637_write_byte(uint8_t data)
{
    uint8_t i;
    int ack = -1;

    tm1637_dio_output();
    for (i = 0; i < 8; i++) {
        tm1637_clk_low();
        tm1637_delay();
        /* 先准备数据位，再拉高 CLK，芯片在上升沿采样 */
        if (data & 0x01) {
            tm1637_dio_high();
        } else {
            tm1637_dio_low();
        }
        tm1637_delay();
        tm1637_clk_high();
        tm1637_delay();
        data = (uint8_t)(data >> 1);    /* LSB 先出 */
    }

    /* 第 8 个时钟下降沿后释放总线，准备读 ACK */
    tm1637_clk_low();
    tm1637_dio_input();
    tm1637_delay();

    /* 第 9 个时钟：芯片在 DIO 上拉低表示应答 */
    tm1637_clk_high();
    tm1637_delay();
    ack = (tm1637_dio_read() == 0) ? 0 : -1;
    tm1637_clk_low();
    tm1637_delay();

    tm1637_dio_output();
    return ack;
}

/* ---------- 段码表 ----------
 * TM1637 位序（bit0~bit7）：A B C D E F G DP
 *   0x3F=0  0x06=1  0x5B=2  0x4F=3  0x66=4
 *   0x6D=5  0x7D=6  0x07=7  0x7F=8  0x6F=9
 */
static const uint8_t g_tm1637_digit_seg[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

/* 常用字母段码（覆盖本项目状态显示所需字符：On/OFF/b/L） */
static uint8_t tm1637_char_to_seg(char c)
{
    switch (c) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return g_tm1637_digit_seg[c - '0'];
        case 'A': case 'a': return 0x77;
        case 'b': case 'B': return 0x7C;
        case 'C': case 'c': return 0x39;
        case 'd': case 'D': return 0x5E;
        case 'E': case 'e': return 0x79;
        case 'F': case 'f': return 0x71;
        case 'G': case 'g': return 0x3D;   /* A+C+D+E+F */
        case 'H': case 'h': return 0x76;
        case 'L': case 'l': return 0x38;
        case 'N': case 'n': return 0x54;
        case 'O': case 'o': return 0x3F;
        case 'P': case 'p': return 0x73;
        case 'r': case 'R': return 0x50;   /* E+G，小写 r 形状 */
        case 't': case 'T': return 0x78;   /* D+E+F+G */
        case 'U': case 'u': return 0x3E;
        case 'Y': case 'y': return 0x6E;   /* B+C+D+F+G */
        case '-':           return 0x40;   /* 仅中间一横 */
        case '_':           return 0x08;
        case ' ':           return TM1637_SEG_BLANK;
        default:            return TM1637_SEG_BLANK;
    }
}

/* ---------- 对外接口 ---------- */

int tm1637_init(void)
{
    /* 1. 复用为 GPIO */
    uapi_pin_set_mode(TM1637_CLK_PIN, HAL_PIO_FUNC_GPIO);
    uapi_pin_set_mode(TM1637_DIO_PIN, HAL_PIO_FUNC_GPIO);

    /* 2. 空闲态：两根线都是高电平 */
    uapi_gpio_set_dir(TM1637_CLK_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_dir(TM1637_DIO_PIN, GPIO_DIRECTION_OUTPUT);
    tm1637_dio_high();
    tm1637_clk_high();
    tm1637_delay();

    /* 3. 【关键】显示选择器：GPIO2=0、GPIO9=1 → 选中数码管。
     *    本底板数码管与点阵共用一个显示通道，不设选择器则 TM1637 不会显示。 */
    uapi_pin_set_mode(TM1637_SEL_LOW_PIN, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(TM1637_SEL_LOW_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(TM1637_SEL_LOW_PIN, GPIO_LEVEL_LOW);

    uapi_pin_set_mode(TM1637_SEL_HIGH_PIN, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(TM1637_SEL_HIGH_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(TM1637_SEL_HIGH_PIN, GPIO_LEVEL_HIGH);

    osal_msleep(10);   /* 等选择器电平稳定后再开始通信 */

    /* 4. 清屏并设置中等亮度（0~7，这里取 3） */
    tm1637_clear();
    tm1637_set_brightness(3);

    printf("tm1637 init ok, clk=P%d dio=P%d, selector: P%d=0 P%d=1\r\n",
           TM1637_CLK_PIN, TM1637_DIO_PIN, TM1637_SEL_LOW_PIN, TM1637_SEL_HIGH_PIN);
    return 0;
}

void tm1637_set_brightness(uint8_t level)
{
    if (level > 7) {
        level = 7;
    }
    tm1637_start();
    (void)tm1637_write_byte((uint8_t)(TM1637_CMD_DISPLAY | level));
    tm1637_stop();
}

void tm1637_write_raw(const uint8_t seg[TM1637_DIGIT_NUM])
{
    uint8_t i;

    if (seg == NULL) {
        return;
    }

    /* 第一段：告诉芯片要写显示寄存器，地址自动增加 */
    tm1637_start();
    (void)tm1637_write_byte(TM1637_CMD_DATA_AUTO);
    tm1637_stop();

    /* 第二段：从地址 0xC0 连续写 4 个字节 */
    tm1637_start();
    (void)tm1637_write_byte(TM1637_CMD_ADDR_BASE);
    for (i = 0; i < TM1637_DIGIT_NUM; i++) {
        (void)tm1637_write_byte(seg[i]);
    }
    tm1637_stop();
}

void tm1637_clear(void)
{
    uint8_t seg[TM1637_DIGIT_NUM] = {0};
    tm1637_write_raw(seg);
}

void tm1637_show_text(const char *str)
{
    uint8_t seg[TM1637_DIGIT_NUM] = {0};
    uint8_t i;
    uint8_t len;

    if (str == NULL) {
        return;
    }

    len = (uint8_t)strlen(str);
    if (len > TM1637_DIGIT_NUM) {
        len = TM1637_DIGIT_NUM;
    }

    /* 左对齐显示，右侧补空白 */
    for (i = 0; i < TM1637_DIGIT_NUM; i++) {
        seg[i] = (i < len) ? tm1637_char_to_seg(str[i]) : TM1637_SEG_BLANK;
    }
    tm1637_write_raw(seg);
}

void tm1637_display_test(uint8_t on)
{
    uint8_t seg[TM1637_DIGIT_NUM] = {0};
    uint8_t i;

    if (on) {
        for (i = 0; i < TM1637_DIGIT_NUM; i++) {
            seg[i] = (uint8_t)(0xFF);   /* 所有段 + 小数点全亮 */
        }
    }
    tm1637_write_raw(seg);
}
