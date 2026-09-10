# Note4 开放固件平台

[English](README.md) | 简体中文

本项目是由 **[Tinnci](https://github.com/Tinnci)** 维护的独立固件框架与应用平台，适用于搭载 SSD2683 黑白墨水屏的 ESP32-S3 设备（基于 Note4 硬件布局）。

项目保留原有板级支持和 SSD2683 显示驱动作为经过验证的硬件基线，并在其上增加系统服务、多应用运行时、电子书引擎、稳定 SDK、伴侣设备连接和受控维护接口。

项目目标是让应用通过受控接口使用显示、输入、电源、时间、存储和连接能力，而不直接操作 GPIO、SPI、裸分区、ESP-NimBLE 或 FreeRTOS 对象。工程只依赖本目录中的组件和 ESP-IDF Component Manager 下载的官方依赖，不连接 NOTE4 商业固件，也不依赖 LVGL。本项目不是完整的 NOTE4 消费版固件或云服务。

---

## 声明与商标说明

> [!NOTE]
> **NOTE4** 与 **ZECTRIX** 是 Zectrix Lab / 相关权利人的产品名称或商标。  
> 本代码库为独立的第三方开源社区项目，**不是**官方发布的固件，与 Zectrix Lab 亦无官方附属、赞助或背书关系。
>
> 刷入第三方固件存在覆盖连接设备上现有软件的风险，操作前请确认硬件版本与准确串口。

## 从参考 Demo 到应用平台

仓库基于上游
[`itopinion/zectrix-note4-epd-demo`](https://github.com/itopinion/zectrix-note4-epd-demo)
的 `ca285c98` 提交建立；来源说明见 [UPSTREAM.md](UPSTREAM.md)。

| 保留的上游基线 | 本仓库新增能力 |
| --- | --- |
| SSD2683 1bpp 全刷、局刷和 4bpp 显示路径 | 可复现 ESP-IDF 工具链、构建溯源、硬件验收与原厂恢复流程 |
| NOTE4 板级适配和外设访问 | 显示、输入、电源、时间、存储和系统服务的单一所有权 |
| 图库 UI 和硬件能力展示 | 含 Launcher、Reader、Settings、Diagnostics、Clock 的静态应用运行时 |
| Wi-Fi RF、音频、RTC、充电、LED、按键、NFC 和电池自检 | 具备兼容性与架构检查的源码稳定 C++17 SDK v1 |
| 基础设备交互和关机流程 | 版本化伴侣协议、持久同步、安全 BLE、Android 伴侣端和 NFC 辅助注册 |
| 面向硬件的串口诊断 | 有资源边界的维护 CLI、平台诊断和交互式 Host 模拟器 |

## 开发状态

项目按 [docs/ROADMAP.md](docs/ROADMAP.md) 中的依赖关系和阶段门推进。

| 阶段 | 状态 | 结果 |
| --- | --- | --- |
| M1 | 已完成 | 可复现上游基线、硬件验收与原厂恢复 |
| M2 | 已完成 | 平台统一管理显示、输入、电源、时间、存储和系统服务 |
| M3 | 已完成 | 静态应用生命周期和首批内置应用 |
| M4 | 已完成 | 源码稳定 SDK v1 和统一软硬件退出门 |
| C1 | 进行中 | 伴侣协议、持久同步、安全 BLE/Android 路径和 NFC 辅助注册；完整硬件验收尚未结束 |
| D1 | 进行中 | USB 会话、平台诊断、日志流和 Host 模拟器已实现；输入观察和硬件验收尚未结束 |
| M5 | 进行中 | A/B 分区校验、固件流式校验和启动确认看门狗已实现；升级交付流程与硬件验收尚未结束 |
| R1 | 进行中 | 最小脏矩形局刷、相同画面跳过与自适应全刷策略已实现；硬件验收尚未结束 |
| L1 | 已实现 | 常驻状态栏、场景导航、TXT/EPUB 流式阅读、局域网书库管理与待机画报；实际睡眠唤醒与待机电流仍待硬件测量 |
| S1 | 进行中 | 已接入按接口类型查询的服务注册表和统一生命周期；Kconfig 裁剪与应用条件装配将在 S1.2/S1.3 实现 |

[服务注册表](docs/SERVICE_REGISTRY.md) 使用 16 个固定槽位，统一服务启动和失败清理。
注册表自身不分配堆内存；缺失或已停止的服务返回空指针，现有 Platform 访问器仍返回
同一组服务实例。

> [!IMPORTANT]
> 本项目仅适用于黑白墨水屏版 ZECTRIX NOTE4，不适用于 NOTE4C。烧录本 Demo
> 会替换连接设备上现有的固件，执行烧录命令前请确认设备型号和准确串口。

![脚印局刷动画预览](main/assets/snow_path_footprints_preview.png)

## 硬件基线

- 400 × 300 SSD2683 黑白墨水屏
- 1bpp 全刷、1bpp 局刷与 4bpp / 16 灰阶全刷
- 灯塔、六步脚印动画和高对比度灰阶山景展示
- Wi-Fi RF、扬声器/麦克风回环、RTC、充电、电池、LED、三按键和 NFC 自检
- Flash、PSRAM、MAC 地址、外设与电源状态设备信息页
- 内置 TRMNL16 界面字库及支持 16px/24px 的 Unifont 中日韩阅读字库
- 独立 SPIFFS 书库中的 TXT/EPUB 流式阅读、NVS 断点保存与手机持久进度同步
- 临时 Wi-Fi 热点或已保存家庭网络上的浏览器传书、下载与删除，会话结束自动关闭 Wi-Fi
- 可保存的日历仪表盘、山水画报与空白隐私屏，展示日期和最近保存的阅读进度
- 长按下键 3 秒显示所选待机画面、关闭外设并关机
- ZECTRIX Lab 以 MIT License 开源

## 构建和烧录

需要项目已验证的 ESP-IDF v5.5.2 基线：

```bash
# 请在仓库根目录执行以下命令。
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

根据实际情况修改串口。退出监视器使用 `Ctrl+]`。第一次构建会下载音频编解码器
组件。详细说明见 [docs/QUICK_START.md](docs/QUICK_START.md)。

当前分区布局保留 factory 和 NVS 地址，增加两个 3 MiB OTA 槽位。试运行固件须在
启动确认期限内完成初始化及 Launcher 首帧渲染。首次安装要求、回滚行为、分块 CRC
和镜像头校验接口及独立配置编译命令见 [ADR-0005](docs/adr/0005-ab-ota-boot-confirmation.md)。

## 电子书阅读

主菜单进入 **BOOK READER**，用上下键选书、OK 打开。阅读时上下键翻页，OK 打开
字号与阅读位置选项，长按 OK 返回书库。每次显示成功后保存进度；Android 伴侣端
可以查看进度并回传阅读位置，设备端需手动选择 **USE PHONE POSITION** 才会跳转。

编译会从 `books/` 生成 `build/books.bin`。使用 `idf.py -p 串口 books-flash` 单独
安装书库；该命令替换整个书籍分区，普通固件刷写保留书库。通过
`-D "ZECTRIX_BOOKS_DIR=/书籍目录的绝对路径"` 可选用自己的 TXT/EPUB 目录。
首次升级须安装新分区表，完整步骤与格式限制见 [docs/READER.md](docs/READER.md)。

首次安装书库后，进入 **SEND BOOKS**，选择创建 Note4 热点或使用已保存的家庭网络。
手机或电脑连接对应网络，在浏览器打开设备显示的地址并输入访问码。热点密码与访问码
相同。拖入 TXT/EPUB 文件后点击 **Upload & finish**，完成后自动关闭 Wi-Fi，设备按
OK 即可阅读。网页还支持下载和删除书籍；同名文件不会被覆盖。使用说明与会话时限见
[docs/BOOK_TRANSFER.md](docs/BOOK_TRANSFER.md)。

## Host 维护 CLI

Linux 和 macOS 上可用 C++17 编译器运行维护 CLI。模拟器复用固件的解析器、终端
会话和诊断执行器，硬件数据为模拟值，无需 ESP-IDF 或连接开发板。

```bash
bash tools/run-cli-host.sh
# Build once for repeated runs or piped commands.
bash tools/build-cli-host.sh
build-host/zectrix-cli-host --owner-delay-ms 500 --log-burst 80
printf 'sysinfo\nheap\nepd-inspect\n' | build-host/zectrix-cli-host
```

输入 `help` 查看命令。`Ctrl+C` 取消命令，`Ctrl+R` 重连会话，`Ctrl+D` 退出。
`--owner-delay-ms` 可模拟诊断回复延迟，期间终端保持响应；`--log-interval-ms 0`
关闭周期日志。管道输入按顺序执行命令，EOF 后完成待处理回复并取消日志流。

运行 `bash tools/test-cli-host.sh` 验证终端和管道交互，或运行
`bash tools/test-host.sh` 执行完整 Host 测试。测试需要 Python 3 和
[uv](https://docs.astral.sh/uv/getting-started/installation/)，仅使用 Python 标准库。
执行模型和资源限制见[维护 CLI 契约](docs/MAINTENANCE_CLI_CONTRACT.md)。

## 目录结构

```text
components/zectrix_epd/       SSD2683 墨水屏公开驱动
components/zectrix_board/     NOTE4 引脚与外设适配层
components/zectrix_demo_ui/   画布、点阵字库与英文演示 UI
components/zectrix_self_test/ 硬件自检实现
components/zectrix_platform/  平台组合根
components/zectrix_reader/    TXT/EPUB 流式排版、字库与书签
components/zectrix_*          系统服务与应用运行时
android-companion/            开发中的 Android BLE/NFC 伴侣端
protocol/                     跨端协议黄金向量
main/assets/                  内嵌显示素材
books/                        默认书库镜像的源目录
tools/                        主机测试、检查与素材转换工具
docs/                         架构、契约与验收记录
```

## 按键

主屏采用日历/阅读概览卡与应用磁贴。概览卡可继续最近一次本地阅读，独立的
**BOOK READER** 磁贴进入书库选书；无记录时概览卡也进入书库。上下键依次经过
概览卡和从左到右、从上到下的磁贴。**TOOLS** 收纳连接、展示、自检及设备信息，
长按 OK 返回主屏，再次打开工具列表会恢复原选择。禁用阅读或网络模块后自动
收起对应入口。详见 [主屏设计与验证](docs/HOME.md)。

| 操作 | 功能 |
| --- | --- |
| 按下上键 | 上一项 |
| 按下下键 | 下一项 |
| 单击 OK | 选择或确认 |
| 长按 OK 1.5 秒 | 返回或取消 |
| 长按下键 3 秒 | 显示所选待机画面、关闭外设并关机 |

电池供电时会释放 GPIO17 电源锁存；USB 供电时会在显示待机画面后进入深度睡眠。
关机后松开下键，再按一次即可唤醒。开机后也需先松开按键，避免误触发关机。

打开 **SLEEP COVER**，用上下键选择日历仪表盘、山水画报或空白隐私屏；OK 保存
并预览，再按 OK 休眠，长按 OK 返回。仪表盘展示最近保存的书名与进度，并用
**AS OF** 标注快照时间；休眠期间不会自动更新。时钟无有效日期时显示
**TIME NOT SET**。设置保存失败会显示 **NOT SAVED**，本次开机仍可使用所选样式。
操作、按键释放超时后的恢复方法与验证范围见 [docs/SLEEP_COVER.md](docs/SLEEP_COVER.md)。

## 展示内容

- 自动展示：依次轮播 1bpp 全刷灯塔、1bpp 局刷六步脚印、4bpp 十六灰阶山水画。
- 显示图库：可单独运行三个刷新场景并查看格式、数据量、耗时和返回值。
- 硬件测试：Wi-Fi RF、扬声器/麦克风声学回环、RTC、充电、电池、LED、三按键和 NFC。
- 设备信息：Flash、PSRAM、MAC、RTC/NFC 状态、电池电压及充电状态。
- 关于页面：Zectrix Lab 与 MIT License 信息。

4bpp 刷新前固定执行白色 1bpp 全刷，降低上一画面的残影。显示服务在两次全刷之间
最多执行 8 次局刷；单次大幅黑白翻转或累计像素变化达到阈值时提前全刷，相同画面
跳过刷新。

## 量产配置

运行 `idf.py menuconfig`，进入 **Zectrix hardware showcase**：

- RF 目标 SSID 留空：扫描到任意 AP 即按展示模式通过。
- 配置目标 SSID：必须连续 3 次找到该 SSID，且 RSSI 达到门限，默认 `-70 dBm`。
- NFC URL 默认 `https://www.zectrix.com`；测试会备份 NFC 用户区，临时写入并
  回读验证，测试结束后恢复原数据。

## 开源与许可证说明

Copyright (c) 2026 Zectrix Lab  
Copyright (c) 2026 Tinnci  
本项目采用 [MIT License](LICENSE) 开源。第三方依赖和字库授权见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。EPD 集成接口见 [docs/EPD_API.md](docs/EPD_API.md)，硬件引脚见 [docs/HARDWARE.md](docs/HARDWARE.md)，测试判定见 [docs/TEST_CRITERIA.md](docs/TEST_CRITERIA.md)。欢迎参与改进，提交前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 致谢

感谢开源硬件与软件社区的贡献与启发：

- **[ZECTRIX Lab](https://wiki.zectrix.com/)** — 研发了出色的 Note4 硬件，并开源了初始参考演示工程（`itopinion/zectrix-note4-epd-demo`）。
- **[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader)** — 为墨水屏流式排版与局域网文件传输提供了良好的架构参考。
- **[Flipper Zero](https://github.com/flipperdevices/flipperzero-firmware)** — 为轻量场景状态机与 CDC-ACM 终端交互提供了设计灵感。
- **Heavyweight Type Foundry 与 GNU Unifont** — 提供基于 SIL OFL 协议的开源字库资源。

## 官方链接

- [ZECTRIX NOTE4 产品网页](https://www.zectrix.com/note4.html)
- [ZECTRIX 开发者 Wiki](https://wiki.zectrix.com/)
