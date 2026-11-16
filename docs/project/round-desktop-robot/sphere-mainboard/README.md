# 球形桌宠主板 A0

[返回圆形桌面机器人设计总览](../README.md)

2026-11-16。无舵机产品主板。当前交付为可编辑的板级布局草图和电气连接定义，接下来需要器件级原理图、真实封装及 PCB 布线。

| 电路 | 连接定义 |
| --- | --- |
| USB-C 设备口 | 5 V VBUS 经保护进入 5 V 电源网，CC1、CC2 各独立接 5.1 kΩ 到地。D−、D+ 分别接 GPIO19、GPIO20，靠模组各预留 22 Ω 串阻与不装的对地电容，插座附近安排数据线静电保护。 |
| 主控与电源 | ESP32-S3-WROOM-1-N16R8，3.3 V 稳压供主控、屏幕与麦克风。稳压器、保护件、电容要按实际负载和 USB 电流预算选型。EN 预留 10 kΩ 上拉和 1 µF 到地，复位键拉低 EN。GPIO0 留 BOOT 键和上拉。 |
| 可拆屏幕 | 3V3、GND、MOSI GPIO10、CLK GPIO9、CS GPIO12、DC GPIO11、RST GPIO21、BL GPIO1。这里是逻辑网名，实际屏幕接口次序和背光驱动须随选定屏幕确认。 |
| 可拆麦克风小板 | 3V3、GND、WS GPIO4、BCLK GPIO5、从麦克风到主控的数据 GPIO6。麦克风型号及通道设置随样品确认。 |
| 功放和喇叭 | MAX98357A 候选，GPIO7 为数据，GPIO15 为 BCLK，GPIO16 为 LRCLK，暂从 5 V 网供电。喇叭接 OUT+、OUT−，两端均与系统地隔离。增益、关断方式和供电滤波按器件参考电路定。 |
| 实体按键 | 对话 GPIO47，音量加 GPIO40，音量减 GPIO39，低电平有效，实际按键形态随外壳确定。 |
| 调试 | 原生 USB 调试，另预留 GND、TX GPIO43、RX GPIO44、EN 和 GPIO0 测试点。 |

布局草图可用 KiCad PCB 编辑器打开，候选板框 46 × 54 mm。元件以丝印或用户图层矩形标示，图中没有可用于制造的焊盘、走线或钻孔。固定孔、最终尺寸、天线禁布区域、USB 插座封装及屏幕、喇叭、麦克风接口须在产品联合布局中确认。

参考[乐鑫原理图检查表](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)和[功放数据手册](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)。

## A0 样件选型与电源预算

| 项目 | 样件方案 |
| --- | --- |
| 屏幕 | 微雪 1.69 寸无触摸 LCD，ST7789V2，240 × 280，模块 31.5 × 39 mm。厂家给出的最大耗电为 3.3 V、90 mA。样件沿用已验证的 SPI 接线和背光控制，载板连接器须对照厂家线序与实物锁扣。 |
| 麦克风 | TDK ICS-43434，I²S 底部拾音，典型 490 µA。A0 使用可更换麦克风小板，供电预算预留 2 mA，确认通道选择、数据位宽和采样时序后复测。 |
| 功放 | MAX98357A，标准 I²S，5 V 供电，桥接输出。喇叭两端均接功放输出。按发声实测设置增益和最大音量。 |
| 喇叭 | Visaton K 28 WP 8 Ω，标称 28 mm、1 W，用于声学样件。背腔、开孔和固定座按样件与厂家机械图核对。厂家页面与 PDF 的开孔尺寸不同，暂不据此定孔。 |
| 降压 | TI TPS62162 固定 3.3 V、1 A。参考电路为 2.2 µH、输入 10 µF、输出 22 µF。具体电感饱和电流、电容偏压降额和 PCB 布局按参考设计核对。 |

3.3 V 电源预算按乐鑫建议提供 500 mA 的主控供电能力，另计屏幕 90 mA、麦克风 2 mA、其余预留 58 mA，合计设计目标 650 mA。500 mA 是供电能力预算。按 85% 的暂定效率，降压部分在 5 V 侧约需 505 mA，损耗约 0.38 W。功放按 1 W 音频输出、80% 暂定效率计算，5 V 侧约需 250 mA。加 100 mA 输入余量，设计估算约 855 mA，整机拟用 5 V、至少 1.5 A 的 USB-C 电源。量产前须检测 USB-C 源的供电能力，限制低电流源下的高负载，或明确使用独立供电调试。以上为预算，闭壳温升、音质和峰值电流须实测。

资料：[微雪 LCD](https://www.waveshare.com/wiki/1.69inch_LCD_Module)、[TDK 麦克风](https://product.tdk.com/en/search/sw_piezo/mic/mems-mic/info?part_no=ICS-43434)、[Visaton 喇叭](https://www.visaton.de/en/products/drivers/miniature-speakers/k-28-wp-8-ohm)、[TI 降压参考电路](https://www.ti.com/lit/ds/symlink/tps62162.pdf)。正式 EDA 原理图的电源替换及外设接线仍待实施。

嘉立创工程「圆形桌宠主板_A0」已查到 TPS62162DSGR、VLF3012ST-2R2M1R4、MAX98357AETE+T、ICS-43434，以及用于定制锁扣线束的 JST GH 8 针和 5 针库器件。屏幕端按微雪原接口配线，主板端可用 JST GH，两个接口按各自针序做转接线。库搜索命中仍须核对厂家焊盘图及贴装供货。
