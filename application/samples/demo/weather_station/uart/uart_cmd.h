#ifndef UART_CMD_H
#define UART_CMD_H

#include <stdbool.h>

/**
 * @brief 串口指令接收模块（PC 下发指令响应，考核硬性要求）
 *
 * 通过 UART 接收中断（空闲触发）收取 PC 串口助手下发的指令，解析后置位
 * 对应标志，由主循环轮询取走并执行。串口与 MQTT 双通道下发指令，体现
 * "串口数据上报 + PC 下发指令响应"的完整闭环。
 *
 * 指令（大小写均可，忽略回车换行）：
 *   QUERY   -> 立即上报一次（等价于 MQTT 的 QUERY）
 *   NET     -> 立即刷新一次网络天气
 *   SHOW    -> 立即切换数码管显示下一项
 *   其它    -> 串口回显
 */

/**
 * @brief 初始化串口指令接收（引脚复用 + UART 配置 + 注册接收回调）
 * @return 0 成功，-1 失败
 */
int uart_cmd_init(void);

/**
 * @brief 泵一下串口指令（非阻塞，供主循环每拍调用）
 *
 * 由主循环在节拍内调用，消费已收到的指令并置位对应标志。
 */
void uart_cmd_poll(void);

/**
 * @brief 取走"立即上报"标志（收到 QUERY 指令）
 * @return true = 有一次待处理的上报请求
 */
bool uart_cmd_take_query_flag(void);

/**
 * @brief 取走"立即刷新网络天气"标志（收到 NET 指令）
 * @return true = 有一次待处理的刷新请求
 */
bool uart_cmd_take_net_flag(void);

/**
 * @brief 取走"立即切换显示"标志（收到 SHOW 指令）
 * @return true = 有一次待处理的切换请求
 */
bool uart_cmd_take_show_flag(void);

/**
 * @brief 取走"扫脚测试"标志（收到 LEDTEST 指令）
 * @return true = 有一次待处理的扫脚测试请求
 */
bool uart_cmd_take_ledtest_flag(void);

#endif
