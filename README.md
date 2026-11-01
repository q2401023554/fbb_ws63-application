# fbb_ws63 · application 层

海思 Hi3863（WS63 / 星闪 NearLink）SDK 的 `src/application` 应用层快照，含本人实现的 **桌面智能气象站** 与官方各外设样例。

> 为什么只有 application：完整 SDK 约 1.6 GB、近 2 万个文件，其中交叉编译器、Wi-Fi 驱动静态库、协议栈占绝大部分，属于厂商分发物。应用层才是自己写的部分，单独成库便于阅读与复用。

## 本人实现

### 桌面智能气象站 · `application/samples/demo/weather_station/`

DHT11 温湿度采集 → TM1637 数码管本地显示 → 联网取 Open-Meteo 天气做对照 → MQTT 周期上报并由上位机看板展示。

技术要点：

- **单总线时序**：DHT11 驱动含校验和验证与超时保护，读失败不阻塞主循环。
- **数码管轮播**：4 位数字管容纳温湿度两组值，本地值带小数点、网络值不带，来源一眼可辨。
- **三层界面并存**：数码管（本地）、串口指令（有线）、MQTT + Web 看板（远程），三者不互相阻塞。
- **反向控制**：板子不只上报，还订阅 `subTopic` 接收 `QUERY` 指令立即回传一帧。
- **状态指示**：RGB 三色灯区分心跳（绿）与异常（红），开机红绿蓝自检，接线错一根就能看出来。
- **参数集中**：引脚、周期、MQTT 参数全在 `app_config.h`，换接线不动驱动源码。

接线表、menuconfig 勾选项、常见故障排查（含实拍图）见该目录下的 `README.md`。

## 其它内容

| 目录 | 内容 |
| :--- | :--- |
| `application/samples/bt` | 蓝牙样例 |
| `application/samples/peripheral` | GPIO / UART / I2C / SPI / PWM 等外设样例 |
| `application/samples/radar` | 雷达相关样例 |
| `application/samples/wifi` | Wi-Fi STA / SoftAP / MQTT 样例 |
| `application/samples/demo` | 官方 demo 集合（含本人的气象站）|
| `application/ws63` | LiteOS 应用与产测固件工程配置 |

## 怎么用

本仓库对应 SDK 中的 `src/application` 目录，可直接覆盖回去：

```bash
git clone https://github.com/q2401023554/fbb_ws63-application.git
cp -r fbb_ws63-application/application/* <SDK>/src/application/
```

编译流程（HiSpark Studio）：

1. 打开 SDK 工程 → menuconfig；
2. 勾选 `ENABLE_DEMO_SAMPLE`，再勾选需要的 demo（气象站为 `Support Weather Station Sample.`）；
3. **取消其它 demo**：LED、wifi_sensor_mqtt 等会与本工程争用 GPIO10 / GPIO13；
4. 保存 → 编译 → 烧录，串口波特率 115200、编码 UTF-8。

## 与上游的差异

- 移除 `samples/demo_backup_*`（本地备份目录，不属于上游）。
- 移除 `ws63/ws63_liteos_mfg/*.bin`（产测固件二进制）。
- `weather_station` 中的热点名、密码、MQTT 服务器地址替换为占位值，运行前需自行填写。
- 其余文件与上游保持一致，未做改动。

## License

`src/applications` 目录在上游采用 Apache License 2.0，本仓库沿用，见 `LICENSE` 与 `NOTICE`。
`weather_station` 为本人原创代码，同样以 Apache-2.0 释出。
