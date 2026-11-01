/**
 * TM1637 四位数码管驱动（两线串行接口：CLK + DIO）
 *
 * 硬件：TM1637 驱动芯片，本工程用于「4 位 7 段数码管」显示灯具状态。
 * 与 MAX7219 不同，TM1637 只有两根信号线，且 DIO 是双向的
 * （主机写数据时输出，接收 ACK 时切换为输入），因此必须动态切换方向。
 *
 * ─── 协议要点（见 TM1637 数据手册）───
 *   1. 空闲态：CLK 与 DIO 均为高电平。
 *   2. 起始条件：CLK 为高时，DIO 由高变低。
 *   3. 停止条件：CLK 为高时，DIO 由低变高。
 *   4. 数据位：CLK 为低时主机准备数据，CLK 上升沿芯片采样；字节按 LSB（最低位）先出。
 *   5. 每个字节后第 8 个时钟下降沿释放 DIO，第 9 个时钟读 ACK（芯片拉低表示应答）。
 *   6. 写显示流程：起始 -> 命令 0x40（自动地址增加，普通写显示）-> 停止；
 *                  起始 -> 0xC0（起始地址 0）-> 4 字节段码 -> 停止；
 *                  起始 -> 0x88|亮度（开显示）-> 停止。
 *
 * ─── 接线（本工程）───
 *   数码管模块直接插在底板右侧的扩展板插座上，由插座供电，无需额外导线。
 *   插座已把 TM1637 的两根信号线引到：
 *       CLK -> P14 (时钟)
 *       DIO -> P13 (数据，双向)
 *
 * ─── 显示选择器（关键！）───
 *   本底板在数码管与 8x8 点阵之间共用一个显示通道，需要先切换选择器：
 *       GPIO2 = 0  且  GPIO9 = 1   → 选中数码管
 *   如果不设置选择器，TM1637 无论发什么数据都不会显示。
 *   这两个引脚由软件配置成 GPIO 输出，不需要接导线。
 */
#ifndef TM1637_H
#define TM1637_H

#include <stdint.h>

/* ---------- 引脚定义（可由 Kconfig 覆盖） ---------- */
#ifdef CONFIG_LIGHTCTL_TM1637_CLK_PIN
#define TM1637_CLK_PIN CONFIG_LIGHTCTL_TM1637_CLK_PIN
#else
#define TM1637_CLK_PIN 14
#endif

#ifdef CONFIG_LIGHTCTL_TM1637_DIO_PIN
#define TM1637_DIO_PIN CONFIG_LIGHTCTL_TM1637_DIO_PIN
#else
#define TM1637_DIO_PIN 13
#endif

/* ---------- 显示选择器引脚（可由 Kconfig 覆盖） ----------
 * GPIO2 输出低电平、GPIO9 输出高电平 → 选中数码管。
 * 这两个引脚固定，不随 TM1637 的 DIO/CLK 变化。
 */
#ifdef CONFIG_LIGHTCTL_TM1637_SEL_LOW_PIN
#define TM1637_SEL_LOW_PIN CONFIG_LIGHTCTL_TM1637_SEL_LOW_PIN
#else
#define TM1637_SEL_LOW_PIN 2
#endif

#ifdef CONFIG_LIGHTCTL_TM1637_SEL_HIGH_PIN
#define TM1637_SEL_HIGH_PIN CONFIG_LIGHTCTL_TM1637_SEL_HIGH_PIN
#else
#define TM1637_SEL_HIGH_PIN 9
#endif

/* 数码管位数 */
#define TM1637_DIGIT_NUM 4

/* TM1637 命令字 */
#define TM1637_CMD_DATA_AUTO  0x40    /* 写显示寄存器，地址自动增加 */
#define TM1637_CMD_ADDR_BASE  0xC0    /* 显示寄存器起始地址 */
#define TM1637_CMD_DISPLAY    0x88    /* 开显示，低 3 位为亮度（0~7） */

/* 不显示（消隐）的段码 */
#define TM1637_SEG_BLANK      0x00

/**
 * @brief 初始化 TM1637：配置 DIO/CLK 为输出并拉到高电平，设置显示选择器，清屏
 * @return 0 成功，-1 失败
 */
int tm1637_init(void);

/**
 * @brief 设置显示亮度并开显示
 * @param level 0~7，越大越亮
 */
void tm1637_set_brightness(uint8_t level);

/**
 * @brief 清空 4 位数码管（全部消隐）
 */
void tm1637_clear(void);

/**
 * @brief 显示 4 个原始段码
 * @param seg 长度 4 的段码数组，seg[0] 为最左边一位
 */
void tm1637_write_raw(const uint8_t seg[TM1637_DIGIT_NUM]);

/**
 * @brief 显示最多 4 个字符，不足右侧补空白（左对齐）
 * @param str 字符串，如 "On"、"OFF"、"b80"、"L46"
 */
void tm1637_show_text(const char *str);

/**
 * @brief 全亮测试：4 位所有段全亮，用于验证接线
 * @param on 1 = 全亮，0 = 清屏
 */
void tm1637_display_test(uint8_t on);

#endif
