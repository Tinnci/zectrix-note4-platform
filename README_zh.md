# ZECTRIX NOTE4 开放固件平台

[English](README.md) | 中文

本项目将上游 ZECTRIX NOTE4 ESP-IDF 硬件 Demo 演进为一个可复现、低功耗的应用
平台。项目保留原有板级支持和 SSD2683 显示驱动作为经过验证的硬件基线，并在其上
增加系统服务、多应用运行时、稳定 SDK、伴侣设备连接和受控维护接口。

项目目标是让应用通过受控接口使用显示、输入、电源、时间、存储和连接能力，而不
直接操作 GPIO、SPI、裸分区、ESP-NimBLE 或 FreeRTOS 对象。工程只依赖本目录中
的组件和 ESP-IDF Component Manager 下载的官方依赖，不连接 NOTE4 商业固件，
也不依赖 LVGL。本项目不是完整的 NOTE4 消费版固件或云服务。

## 从参考 Demo 到应用平台

仓库基于上游
[`itopinion/zectrix-note4-epd-demo`](https://github.com/itopinion/zectrix-note4-epd-demo)
的 `ca285c98` 提交建立；来源说明见 [UPSTREAM.md](UPSTREAM.md)。

| 保留的上游基线 | 本仓库新增能力 |
| --- | --- |
| SSD2683 1bpp 全刷、局刷和 4bpp 显示路径 | 可复现 ESP-IDF 工具链、构建溯源、硬件验收与原厂恢复流程 |
| NOTE4 板级适配和外设访问 | 显示、输入、电源、时间、存储和系统服务的单一所有权 |
| 图库 UI 和硬件能力展示 | 含 Launcher、Settings、Diagnostics、Clock 的静态多应用运行时 |
| Wi-Fi RF、音频、RTC、充电、LED、按键、NFC 和电池自检 | 具备兼容性与架构检查的源码稳定 C++17 SDK v1 |
| 基础设备交互和关机流程 | 版本化伴侣协议、持久同步、安全 BLE、Android 伴侣端和 NFC 辅助注册 |
| 面向硬件的串口诊断 | 有资源边界的维护 CLI 架构及已测试解析器核心 |

## 开发状态

项目按 [docs/ROADMAP.md](docs/ROADMAP.md) 中的依赖关系和阶段门推进。

| 阶段 | 状态 | 结果 |
| --- | --- | --- |
| M1 | 已完成 | 可复现上游基线、硬件验收与原厂恢复 |
| M2 | 已完成 | 平台统一管理显示、输入、电源、时间、存储和系统服务 |
| M3 | 已完成 | 静态应用生命周期和首批内置应用 |
| M4 | 已完成 | 源码稳定 SDK v1 和统一软硬件退出门 |
| C1 | 进行中 | 伴侣协议、持久同步、安全 BLE/Android 路径和 NFC 辅助注册；完整硬件验收尚未结束 |
| D1 | 进行中 | 受控维护 CLI 核心；服务命令和硬件退出门尚未结束 |
| M5 | 计划中 | 基于实测数据设计 A/B 更新、回滚和恢复架构 |

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
- 内置 TRMNL16 ASCII 点阵字库，无运行时字体或文件系统依赖
- 长按下键 3 秒清屏、关闭外设并关机
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

## 目录结构

```text
components/zectrix_epd/       SSD2683 墨水屏公开驱动
components/zectrix_board/     NOTE4 引脚与外设适配层
components/zectrix_demo_ui/   画布、点阵字库与英文演示 UI
components/zectrix_self_test/ 硬件自检实现
components/zectrix_platform/  平台组合根
components/zectrix_*          系统服务与应用运行时
android-companion/            开发中的 Android BLE/NFC 伴侣端
protocol/                     跨端协议黄金向量
main/assets/                  内嵌显示素材
tools/                        主机测试、检查与素材转换工具
docs/                         架构、契约与验收记录
```

## 按键

| 操作 | 功能 |
| --- | --- |
| 按下上键 | 上一项 |
| 按下下键 | 下一项 |
| 单击 OK | 选择或确认 |
| 长按 OK 1.5 秒 | 返回或取消 |
| 长按下键 3 秒 | 白色全刷清屏、关闭外设并关机 |

电池供电时会释放 GPIO17 电源锁存；USB 供电时无法切断硬件电源，因此清屏后进入
深度睡眠。开机键与下键复用，程序要求开机后先松开按键，避免误触发关机。

## 展示内容

- 自动展示：依次轮播 1bpp 全刷灯塔、1bpp 局刷六步脚印、4bpp 十六灰阶山水画。
- 显示图库：可单独运行三个刷新场景并查看格式、数据量、耗时和返回值。
- 硬件测试：Wi-Fi RF、扬声器/麦克风声学回环、RTC、充电、电池、LED、三按键和 NFC。
- 设备信息：Flash、PSRAM、MAC、RTC/NFC 状态、电池电压及充电状态。
- 关于页面：Zectrix Lab 与 MIT License 信息。

4bpp 刷新前固定执行白色 1bpp 全刷，降低上一画面的残影。菜单局刷累计到一定次数
后自动做全刷，控制 UI 残影。

## 量产配置

运行 `idf.py menuconfig`，进入 **Zectrix hardware showcase**：

- RF 目标 SSID 留空：扫描到任意 AP 即按展示模式通过。
- 配置目标 SSID：必须连续 3 次找到该 SSID，且 RSSI 达到门限，默认 `-70 dBm`。
- NFC URL 默认 `https://www.zectrix.com`；测试会备份 NFC 用户区，临时写入并
  回读验证，测试结束后恢复原数据。

## 开源说明

本 Demo 版权归 Zectrix Lab 所有，采用 [MIT License](LICENSE)。第三方依赖和
字库授权见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。EPD 集成接口见
[docs/EPD_API.md](docs/EPD_API.md)，硬件引脚见
[docs/HARDWARE.md](docs/HARDWARE.md)，测试判定见
[docs/TEST_CRITERIA.md](docs/TEST_CRITERIA.md)。
欢迎参与改进，提交前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 官方链接

- [ZECTRIX NOTE4 产品网页](https://www.zectrix.com/note4.html)
- [ZECTRIX 开发者 Wiki](https://wiki.zectrix.com/)
