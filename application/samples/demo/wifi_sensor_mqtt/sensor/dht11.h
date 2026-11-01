#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>

/*
 * DHT11 数据引脚：开发板 P13（丝印 Pxx == GPIO xx，故取 13）。
 * 使用 3 针 DHT11 模块（模块板自带上拉电阻），接线只需 VCC / DATA / GND 三根。
 */
#define DHT11_PIN 13

/* 读取结果 */
typedef struct {
    uint8_t temperature;   /* 温度，整数部分，单位 ℃ */
    uint8_t humidity;      /* 湿度，整数部分，单位 %RH */
    uint8_t temp_dec;      /* 温度小数部分（DHT11 恒为 0） */
    uint8_t humi_dec;      /* 湿度小数部分（DHT11 恒为 0） */
} dht11_data_t;

/**
 * @brief 初始化 DHT11 数据引脚
 * @return 0 成功，-1 失败
 */
int dht11_init(void);

/**
 * @brief 读取一次温湿度
 * @param data 输出结果
 * @return 0 成功，-1 校验失败/超时
 *
 * 单总线要求两次读取间隔 >= 1s，本工程上报周期 5s 满足要求。
 */
int dht11_read(dht11_data_t *data);

#endif
