/**
 * 串口指令接收模块（PC 下发指令响应）
 *
 * 考核要求："串口数据上报或 PC 下发指令响应功能"。
 * 本模块补齐串口下发指令这一路，与 MQTT 的 QUERY 指令形成双通道控制闭环：
 *   PC 串口助手 -> UART 接收中断 -> 指令解析 -> 置位标志 -> 主循环取走执行。
 *
 * 指令（大小写均可，兼容 CR/LF 换行）：
 *   QUERY -> 立即上报一次；NET -> 立即刷新网络天气；SHOW -> 切换数码管显示项。
 *
 * 复用 UART0（GPIO17=TXD / GPIO18=RXD，PIN_MODE_1），与调试串口打印共用物理串口，
 * 因此接收回调只收指令不回显干扰，打印仍走 osal 的 printf。
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "securec.h"
#include "pinctrl.h"
#include "uart.h"
#include "soc_osal.h"
#include "app_config.h"
#include "uart_cmd.h"

#define UART_CMD_RECV_SIZE           64        /* 指令接收缓存，指令很短，64 足够 */
#define UART_RX_CONDITION_MASK_IDLE  1         /* 接收中断触发：空闲（一帧结束）*/

/* UART0 引脚复用（见 IO 复用表）：GPIO17=UART0_TXD，GPIO18=UART0_RXD */
#define UART_CMD_TX_PIN              GPIO_17
#define UART_CMD_RX_PIN              GPIO_18

/* 接收缓存与标志（接收回调在中断上下文，必须 volatile） */
static uint8_t g_uart_recv[UART_CMD_RECV_SIZE] = { 0 };
static volatile uint16_t g_rx_len = 0;
static volatile bool g_rx_ready = false;

/* 指令标志：由 poll 消费原始数据后置位，由主循环 take 取走 */
static bool g_query_flag = false;
static bool g_net_flag = false;
static bool g_show_flag = false;
static bool g_ledtest_flag = false;

/* UART 接收缓冲区配置（驱动层使用） */
static uart_buffer_config_t g_uart_buffer_config = {
    .rx_buffer = g_uart_recv,
    .rx_buffer_size = UART_CMD_RECV_SIZE
};

/* 引脚复用：GPIO17/18 切换为 UART0 功能 */
static void uart_cmd_gpio_init(void)
{
    uapi_pin_set_mode(UART_CMD_TX_PIN, PIN_MODE_1);   /* GPIO17 -> UART0_TXD */
    uapi_pin_set_mode(UART_CMD_RX_PIN, PIN_MODE_1);   /* GPIO18 -> UART0_RXD */
}

/* UART0 配置：115200 8N1 */
static void uart_cmd_config(void)
{
    uart_attr_t attr = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE
    };
    uart_pin_config_t pin_config = {
        .tx_pin = UART_CMD_TX_PIN,
        .rx_pin = UART_CMD_RX_PIN,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE
    };

    uapi_uart_deinit(UART_BUS_0);   /* 先反初始化，保证可重复初始化 */
    if (uapi_uart_init(UART_BUS_0, &pin_config, &attr, NULL, &g_uart_buffer_config) != ERRCODE_SUCC) {
        printf("[UART] init fail\r\n");
    }
}

/* 接收中断回调：拷贝到缓存并置 ready 标志 */
static void uart_cmd_read_handler(const void *buffer, uint16_t length, bool error)
{
    unused(error);
    if (buffer == NULL || length == 0) {
        return;
    }
    if (length > UART_CMD_RECV_SIZE) {
        length = UART_CMD_RECV_SIZE;
    }
    if (memcpy_s(g_uart_recv, UART_CMD_RECV_SIZE, buffer, length) != EOK) {
        return;
    }
    g_rx_len = length;
    g_rx_ready = true;
}

/* 忽略大小写查找子串（指令均短，简单实现即可） */
static bool str_contains_ci(const char *haystack, const char *needle)
{
    size_t nl = strlen(needle);
    size_t hl = strlen(haystack);
    size_t i;
    size_t j;

    if (hl < nl) {
        return false;
    }
    for (i = 0; i + nl <= hl; i++) {
        for (j = 0; j < nl; j++) {
            char h = haystack[i + j];
            char n = needle[j];
            if (h >= 'a' && h <= 'z') {
                h = (char)(h - 'a' + 'A');
            }
            if (n >= 'a' && n <= 'z') {
                n = (char)(n - 'a' + 'A');
            }
            if (h != n) {
                break;
            }
        }
        if (j == nl) {
            return true;
        }
    }
    return false;
}

/* 解析一条指令，置位对应标志 */
static void uart_cmd_parse(const char *cmd)
{
    if (str_contains_ci(cmd, "LEDTEST")) {
        /* 放在最前面判断，避免被其它关键字误命中 */
        g_ledtest_flag = true;
        printf("[UART] cmd LEDTEST -> sweep GPIO pins for LED\r\n");
    } else if (str_contains_ci(cmd, "QUERY")) {
        g_query_flag = true;
        printf("[UART] cmd QUERY -> report now\r\n");
    } else if (str_contains_ci(cmd, "NET")) {
        g_net_flag = true;
        printf("[UART] cmd NET -> refresh weather now\r\n");
    } else if (str_contains_ci(cmd, "SHOW")) {
        g_show_flag = true;
        printf("[UART] cmd SHOW -> switch display\r\n");
    } else {
        printf("[UART] unknown cmd: %s\r\n", cmd);
    }
}

int uart_cmd_init(void)
{
    uart_cmd_gpio_init();
    uart_cmd_config();
    if (uapi_uart_register_rx_callback(UART_BUS_0, UART_RX_CONDITION_MASK_IDLE, 1,
                                       uart_cmd_read_handler) != ERRCODE_SUCC) {
        printf("[UART] rx callback register fail\r\n");
        return -1;
    }
    printf("[UART] cmd receiver ready (QUERY / NET / SHOW)\r\n");
    return 0;
}

void uart_cmd_poll(void)
{
    if (!g_rx_ready) {
        return;
    }
    g_rx_ready = false;

    /* 保证字符串以 '\0' 结尾 */
    uint16_t len = g_rx_len;
    if (len >= UART_CMD_RECV_SIZE) {
        len = UART_CMD_RECV_SIZE - 1;
    }
    g_uart_recv[len] = '\0';

    uart_cmd_parse((const char *)g_uart_recv);

    memset(g_uart_recv, 0, UART_CMD_RECV_SIZE);
    g_rx_len = 0;
}

bool uart_cmd_take_query_flag(void)
{
    bool flag = g_query_flag;
    g_query_flag = false;
    return flag;
}

bool uart_cmd_take_net_flag(void)
{
    bool flag = g_net_flag;
    g_net_flag = false;
    return flag;
}

bool uart_cmd_take_show_flag(void)
{
    bool flag = g_show_flag;
    g_show_flag = false;
    return flag;
}

bool uart_cmd_take_ledtest_flag(void)
{
    bool flag = g_ledtest_flag;
    g_ledtest_flag = false;
    return flag;
}
