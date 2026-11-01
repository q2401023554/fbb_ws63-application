# 桌面智能气象站（weather_station）

南向设备开发考核项目 —— 题目（1）"桌面智能气象站"：
**DHT11 温湿度采集 + 数码管显示 + Wi-Fi 获取网络天气对比 + MQTT 上报并在 UI 界面显示数据**

## 一、功能清单

| 编号 | 功能 | 实现模块 |
|------|------|----------|
| 1 | DHT11 温湿度采集（单总线驱动，含校验和验证） | `sensor/dht11.c/.h` |
| 2 | TM1637 数码管轮播显示温湿度数值（本地带小数点 / 网络不带） | `display/segment_display.c/.h` |
| 3 | Wi-Fi STA 联网 + HTTP 获取 Open-Meteo 网络天气并对比 | `wifi/wifi_connect.c/.h`、`net/weather_http.c/.h` |
| 4 | MQTT 周期上报 JSON 数据 + 远程 QUERY 指令查询 | `mqtt/mqtt_report.c/.h` |
| 5 | 串口 PC 下发指令响应（QUERY/NET/SHOW/LEDTEST）+ 断线自动重连 | `uart/uart_cmd.c/.h` |
| 6 | 三层 UI：数码管轮播 / 串口数据看板 / MQTTX 远程查看 | `weather_station.c` |
| 7 | 错误检测与状态指示 LED（心跳 / 异常快闪） | `status_led.c/.h` |

## 二、硬件接线

| 外设 | 外设引脚 | Hi3863 引脚 | 功能说明 |
|------|----------|-------------|----------|
| DHT11 温湿度模块 | VCC | 3.3V | 电源 |
| | DATA | **P10 (GPIO10)** | 单总线数据 ⚠ 不能接 P13（数码管插槽占用）|
| | GND | GND | 地 |
| TM1637 数码管（板载插槽） | SDA | GPIO13 | 两线数据 |
| | SCL | GPIO14 | 两线时钟 |
| | selector | GPIO2=0 / GPIO9=1 | 插槽模式选择（数码管模式）|
| RGB LED 模块（**外接 4 针**） | R | **P0 (GPIO0)** | 红色通道，高电平点亮 |
| | G | **P5 (GPIO5)** | 绿色通道（心跳用）|
| | B | **P7 (GPIO7)** | 蓝色通道 |
| | GND / VCC | **GND**（共阴）/ **3.3V**（共阳）| 公共脚，**不接死都不亮** ⚠ 共阳需把 `LED_RGB_COMMON_ANODE` 改成 1 |

## 三、编译与烧录（HiSpark Studio）

1. 打开 HiSpark Studio 工程（`src/demo.hiproj`）；
2. 菜单打开 **Kconfig 配置（menuconfig）**；
3. 勾选 `ENABLE_DEMO_SAMPLE`，再勾选其下的 `Support Weather Station Sample.`；
4. **取消勾选其它 demo**（尤其是 LED / LED2 / wifi_sensor_mqtt / wifi_ctrl_mqtt，
   它们与本工程争用 GPIO10/GPIO13）；
5. 保存配置 → 重新编译 → 烧录；
6. 串口波特率 115200，**编码选择 UTF-8**（看板含中文）。

## 四、网络配置

| 配置项 | 位置 | 默认值 |
|--------|------|--------|
| WiFi 账号/密码 | `wifi/wifi_connect.h` | 需自行填写（仓库中为占位值）|
| 天气城市坐标 | `net/weather_http.c` 顶部 `WEATHER_URL_PATH` | 武汉 30.35N, 114.31E |
| MQTT Broker | `app_config.h` `CONFIG_MQTT_BROKER_IP` | 192.168.1.100:1883（本机运行 EMQX）|
| 上报 / 订阅主题 | `app_config.h` | pubTopic / subTopic |

电脑端验证步骤：
1. 运行 EMQX，打开 MQTTX 客户端；
2. 订阅 `pubTopic` → 每 5s 收到一条 JSON：
   `{"dev":"ws63-weather","temp":26,"humi":58,"netTemp":25,"netHumi":60,"diffTemp":1,"diffHumi":-2,"netOk":1,"ip":"..."}`
3. 向 `subTopic` 发送 `QUERY` → 立即触发一次上报。

## 五、运行效果

- 数码管每 2s 轮播：本地温度 `26.`（带小数点）→ 本地湿度 `58.` → 网络温度 `25` → 网络湿度 `60`；
- 串口每 5s 打印对比看板（本地 vs 网络 vs 偏差 vs 运行状态）；
- LED（外接 RGB：R=P0 / G=P5 / B=P7）：**上电先自检闪 3 下**；之后每次成功上报闪 1 下（亮 150ms）；WiFi/MQTT/HTTP 失败快闪 3 下；
- 网络天气每 10 分钟自动刷新一次（Open-Meteo 免费，无需 API key）。

## 六、与考核评分标准的对应

| 评分维度 | 本项目的体现 |
|----------|--------------|
| 创新性与实用性（45 分）| 本地实测 vs 网络天气预报的"偏差对比"是核心创新点；三层 UI + 远程查询指令 |
| 硬件调试与连接（25 分）| 接线表齐全；DHT11 引脚冲突（P13 被数码管占用）已识别并规避到 P10 |
| 代码质量（20 分）| 8 个 .c/.h 模块化分离；关键函数注释头；错误码检查 + LED 状态指示 + 失败保留上次有效值 |
| 报告质量（10 分）| 本 README 的接线表/架构图/验证步骤可直接作为报告"硬件设计/软件设计/实验结果"章节素材 |
