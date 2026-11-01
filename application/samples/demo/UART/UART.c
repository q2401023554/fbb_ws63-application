/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 *
 * Description: UART sample source (UART0 echo on GPIO17/18, LED on/off via serial cmd). \n
 *
 * History: \n
 * 2026-09-16, Create file (based on teacher's UART lab). \n
 * 2026-09-16, Add LED control: "开灯" -> start RGB flash, "关灯" -> stop. \n
 */

#include "pinctrl.h"
#include "uart.h"
#include "soc_osal.h"
#include "app_init.h"
#include "securec.h"
#include "string.h"
#include "../LED2/led2.h"

/* 接收缓存大小 */
#define UART_RECV_SIZE              128
/* 接收中断触发条件：空闲（一帧数据接收结束即触发） */
#define UART_RX_CONDITION_MASK_IDLE 1

/* 任务优先级与栈大小 */
#define UART_TASK_PRIO              24
#define UART_TASK_STACK_SIZE        0x1000

/* UART0 引脚（见 IO 复用表）：GPIO17 = UART0_TXD，GPIO18 = UART0_RXD，均为 PIN_MODE_1 */
#define UART0_TX_PIN                GPIO_17
#define UART0_RX_PIN                GPIO_18

/* 指令字节序列：兼容串口助手的 UTF-8 与 GBK（ANSI）两种中文编码 */
static const uint8_t g_cmd_led_on_utf8[]  = { 0xE5, 0xBC, 0x80, 0xE7, 0x81, 0xAF };  /* 开灯(UTF-8) */
static const uint8_t g_cmd_led_on_gbk[]   = { 0xBF, 0xAA, 0xB5, 0xC6 };             /* 开灯(GBK) */
static const uint8_t g_cmd_led_off_utf8[] = { 0xE5, 0x85, 0xB3, 0xE7, 0x81, 0xAF };  /* 关灯(UTF-8) */
static const uint8_t g_cmd_led_off_gbk[]  = { 0xB9, 0xD8, 0xB5, 0xC6 };             /* 关灯(GBK) */

/* 接收缓存与标志 */
static uint8_t uart_recv[UART_RECV_SIZE] = { 0 };
static volatile uint16_t uart_rx_len = 0;   /* 本次接收到的数据长度 */
static volatile uint8_t uart_rx_flag = 0;   /* 收到数据的标志 */

/* UART 接收缓冲区配置（驱动层使用） */
static uart_buffer_config_t g_app_uart_buffer_config = {
    .rx_buffer = uart_recv,
    .rx_buffer_size = UART_RECV_SIZE
};

/* 初始化 UART 引脚复用：GPIO17/18 切换为 UART0 功能 */
static void uart_gpio_init(void)
{
    uapi_pin_set_mode(UART0_TX_PIN, PIN_MODE_1);   /* GPIO17 -> UART0_TXD */
    uapi_pin_set_mode(UART0_RX_PIN, PIN_MODE_1);   /* GPIO18 -> UART0_RXD */
}

/* 初始化 UART 配置：波特率 115200、8 数据位、1 停止位、无校验 */
static void uart_init_config(void)
{
    uart_attr_t attr = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE
    };
    uart_pin_config_t pin_config = {
        .tx_pin = UART0_TX_PIN,
        .rx_pin = UART0_RX_PIN,
        .cts_pin = PIN_NONE,   /* 不使用硬件流控 */
        .rts_pin = PIN_NONE
    };

    uapi_uart_deinit(UART_BUS_0);   /* 先反初始化，保证可重复初始化 */
    errcode_t ret = uapi_uart_init(UART_BUS_0, &pin_config, &attr, NULL, &g_app_uart_buffer_config);
    if (ret != ERRCODE_SUCC) {
        osal_printk("uart init failed ret = %02x\r\n", ret);
    }
}

/* 接收中断回调：串口收到数据时由驱动调用，拷贝到缓存并置标志 */
static void uart_read_handler(const void *buffer, uint16_t length, bool error)
{
    unused(error);
    if (buffer == NULL || length == 0) {
        return;
    }
    if (length > UART_RECV_SIZE) {
        length = UART_RECV_SIZE;
    }
    if (memcpy_s(uart_recv, UART_RECV_SIZE, buffer, length) != EOK) {
        return;
    }
    uart_rx_len = length;
    uart_rx_flag = 1;
}

/* 在 buf 中查找 cmd 子串（返回 1 表示命中） */
static int cmd_match(const uint8_t *buf, uint16_t len, const uint8_t *cmd, uint16_t cmd_len)
{
    uint16_t i;

    if (buf == NULL || cmd == NULL || len < cmd_len) {
        return 0;
    }
    for (i = 0; i + cmd_len <= len; i++) {
        if (memcmp(buf + i, cmd, cmd_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* 判断是否为“开灯”指令（UTF-8 或 GBK 任一编码命中即可） */
static int is_led_on_cmd(const uint8_t *buf, uint16_t len)
{
    return cmd_match(buf, len, g_cmd_led_on_utf8, sizeof(g_cmd_led_on_utf8)) ||
           cmd_match(buf, len, g_cmd_led_on_gbk, sizeof(g_cmd_led_on_gbk));
}

/* 判断是否为“关灯”指令（UTF-8 或 GBK 任一编码命中即可） */
static int is_led_off_cmd(const uint8_t *buf, uint16_t len)
{
    return cmd_match(buf, len, g_cmd_led_off_utf8, sizeof(g_cmd_led_off_utf8)) ||
           cmd_match(buf, len, g_cmd_led_off_gbk, sizeof(g_cmd_led_off_gbk));
}

/* UART 任务：初始化 -> 注册回调 -> 循环收数据、执行指令或回显 */
static void *uart_task(const char *arg)
{
    unused(arg);

    /* 1. 配置 UART 引脚复用 */
    uart_gpio_init();
    /* 2. 初始化 UART0 */
    uart_init_config();
    /* 3. 注册接收中断回调（空闲触发） */
    if (uapi_uart_register_rx_callback(UART_BUS_0, UART_RX_CONDITION_MASK_IDLE, 1,
                                       uart_read_handler) == ERRCODE_SUCC) {
        osal_printk("uart0 rx callback register succ\r\n");
    }

    while (1) {
        /* 等待接收数据 */
        while (uart_rx_flag == 0) {
            osal_msleep(10);
        }
        uart_rx_flag = 0;

        /* 确保字符串以 '\0' 结尾，便于打印 */
        if (uart_rx_len >= UART_RECV_SIZE) {
            uart_rx_len = UART_RECV_SIZE - 1;
        }
        uart_recv[uart_rx_len] = '\0';

        /* 指令判断：开灯 / 关灯 / 其他数据回显 */
        if (is_led_on_cmd(uart_recv, uart_rx_len)) {
            osal_printk("LED ON: start RGB flash\r\n");
            led2_flash_start();
        } else if (is_led_off_cmd(uart_recv, uart_rx_len)) {
            osal_printk("LED OFF: stop RGB flash\r\n");
            led2_flash_stop();
        } else {
            /* 打印接收到的数据并原样回显给串口助手 */
            osal_printk("uart rx = [%s]\r\n", uart_recv);
            uapi_uart_write(UART_BUS_0, uart_recv, uart_rx_len, 0);
        }

        memset(uart_recv, 0, UART_RECV_SIZE);
        uart_rx_len = 0;
    }
    return NULL;
}

/* 入口函数：创建 UART 任务 */
static void uart_entry(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)uart_task, 0, "UartTask",
                                      UART_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, UART_TASK_PRIO);
    }
    osal_kthread_unlock();
}

/* Run the uart_entry. */
app_run(uart_entry);
