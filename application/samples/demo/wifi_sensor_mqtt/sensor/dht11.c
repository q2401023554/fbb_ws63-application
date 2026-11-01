/**
 * DHT11 温湿度传感器驱动（单总线 / 自定义微秒时序）
 *
 * 说明：
 *   WS63 SDK 没有 DHT11 驱动，这里用 GPIO + uapi_tcxo_delay_us() 自行实现单总线时序。
 *   时序要点（DHT11 数据手册）：
 *     主机拉低 >= 18ms 作为起始信号 -> 拉高 20~40us 等待应答
 *     从机拉低 80us -> 拉高 80us 作为应答
 *     之后每一位数据以 50us 低电平开始，高电平持续 26~28us 表示"0"，70us 表示"1"
 *   共 40 位：8 位湿度整数 + 8 位湿度小数 + 8 位温度整数 + 8 位温度小数 + 8 位校验和
 *
 * 接线（3 针 DHT11 模块，模块板自带 4.7k 上拉，无需外接电阻）：
 *   VCC -> 3.3V   DATA -> P13 (GPIO13)   GND -> GND
 */
#include <stdio.h>
#include "pinctrl.h"
#include "gpio.h"
#include "tcxo.h"
#include "soc_osal.h"
#include "dht11.h"

/* 单总线时序参数（单位 us） */
#define DHT11_START_LOW_MS   20     /* 主机起始信号：拉低至少 18ms，取 20ms */
#define DHT11_START_HIGH_US  30     /* 主机释放总线后拉高 20~40us */
#define DHT11_TIMEOUT_US     200    /* 等待电平变化的超时时间（宽松些，抗抖动） */
#define DHT11_BIT_NUM        40     /* 数据总位数 */

/*
 * 区分 0/1 的阈值（单位：轮询次数）。
 *
 * 原理：DHT11 每位的低电平固定 50us，随后高电平：
 *   "0" = 26~28us，  "1" = 70us
 * 本实现统计高电平维持了多少轮（每轮 = uapi_tcxo_delay_us(1) + GPIO 读开销，
 * 实测约 2~3us/轮）。
 *
 * 取两者的中间值附近（约 50us）作为分界：
 *   每轮约 2.5us 时 20 轮 ≈ 50us，正好居中，对 0/1 两边都有 ~20us 余量。
 */
#define DHT11_ONE_MIN_LOOPS  20

/* 把引脚切为输出 / 输入（DHT11 是单总线，收发复用同一根线） */
static inline void dht11_set_output(void)
{
    uapi_gpio_set_dir(DHT11_PIN, GPIO_DIRECTION_OUTPUT);
}

static inline void dht11_set_input(void)
{
    uapi_gpio_set_dir(DHT11_PIN, GPIO_DIRECTION_INPUT);
}

static inline void dht11_write(uint32_t level)
{
    uapi_gpio_set_val(DHT11_PIN, (gpio_level_t)level);
}

static inline uint32_t dht11_read_level(void)
{
    return (uint32_t)uapi_gpio_get_val(DHT11_PIN);
}

/**
 * @brief 等待引脚变为指定电平
 * @param level 期望电平
 * @param timeout_us 超时时间（us）
 * @return 0 已到达，-1 超时
 */
static int dht11_wait_level(uint32_t level, uint32_t timeout_us)
{
    uint32_t elapsed = 0;
    while (dht11_read_level() != level) {
        if (elapsed >= timeout_us) {
            return -1;
        }
        uapi_tcxo_delay_us(1);
        elapsed++;
    }
    return 0;
}

int dht11_init(void)
{
    errcode_t ret;

    /* TCXO 未初始化时 uapi_tcxo_delay_us() 会静默失败（直接返回 ERRCODE_FAIL 且不延时），
     * 那样单总线时序会完全错乱且无任何提示，这里先做一次自检。 */
    ret = uapi_tcxo_delay_us(10);
    if (ret != ERRCODE_SUCC) {
        printf("dht11 init fail: tcxo not ready (ret=%d), us delay unavailable\r\n", ret);
        return -1;
    }

    /* 复用为 GPIO，默认输出高电平（空闲态） */
    uapi_pin_set_mode(DHT11_PIN, HAL_PIO_FUNC_GPIO);
    dht11_set_output();
    dht11_write(GPIO_LEVEL_HIGH);
    osal_msleep(100);   /* 上电后等待传感器稳定 */
    printf("dht11 init ok, data pin = GPIO %d\r\n", DHT11_PIN);
    return 0;
}

int dht11_read(dht11_data_t *data)
{
    uint8_t buf[5] = {0};
    uint32_t i = 0;
    uint32_t high_time = 0;

    if (data == NULL) {
        return -1;
    }

    /* ---------- 1. 主机发送起始信号 ---------- */
    dht11_set_output();
    dht11_write(GPIO_LEVEL_LOW);
    osal_msleep(DHT11_START_LOW_MS);    /* 拉低 >= 18ms */
    dht11_write(GPIO_LEVEL_HIGH);
    uapi_tcxo_delay_us(DHT11_START_HIGH_US);
    dht11_set_input();                  /* 释放总线，等待从机应答 */

    /* ---------- 2. 等待从机应答：80us 低 + 80us 高 ---------- */
    if (dht11_wait_level(GPIO_LEVEL_LOW, DHT11_TIMEOUT_US) != 0) {
        return -1;
    }
    if (dht11_wait_level(GPIO_LEVEL_HIGH, DHT11_TIMEOUT_US) != 0) {
        return -1;
    }
    if (dht11_wait_level(GPIO_LEVEL_LOW, DHT11_TIMEOUT_US) != 0) {
        return -1;
    }

    /* ---------- 3. 读取 40 位数据 ---------- */
    for (i = 0; i < DHT11_BIT_NUM; i++) {
        /* 每位的起始：50us 低电平，等它变为高 */
        if (dht11_wait_level(GPIO_LEVEL_HIGH, DHT11_TIMEOUT_US) != 0) {
            return -1;
        }
        /* 统计高电平持续了多少轮（每轮约 2~3us） */
        high_time = 0;
        while (dht11_read_level() == GPIO_LEVEL_HIGH) {
            if (high_time >= DHT11_TIMEOUT_US) {
                return -1;
            }
            uapi_tcxo_delay_us(1);
            high_time++;
        }
        /* 高电平明显更长者判为 1 */
        buf[i / 8] <<= 1;
        if (high_time >= DHT11_ONE_MIN_LOOPS) {
            buf[i / 8] |= 0x01;
        }
    }

    /* ---------- 4. 校验：前 4 字节之和的低 8 位 == 第 5 字节 ---------- */
    if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) {
        return -1;
    }

    data->humidity = buf[0];
    data->humi_dec = buf[1];
    data->temperature = buf[2];
    data->temp_dec = buf[3];

    /* 读完后拉高总线，回到空闲态 */
    dht11_set_output();
    dht11_write(GPIO_LEVEL_HIGH);
    return 0;
}
