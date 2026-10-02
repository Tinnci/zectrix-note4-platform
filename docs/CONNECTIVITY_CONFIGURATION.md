# Connectivity 配置

已实现本地配置、后台下载、页面缓存和唤醒排期；硬件验证未完成。
Companion 协议不变，远程页面使用独立格式。

## 配置与边界

| 配置 | 存储／入口 | 用途 |
| --- | --- | --- |
| 编译能力 | Kconfig、sdkconfig | 裁剪功能，不能由用户配置启用 |
| 资源路由 | `conn_policy` | 自动／仅手机／仅 Wi-Fi／离线 |
| Wi-Fi | `wifi_station` | SSID 与密码一起保存 |
| 远程页面 | `edge_settings`、`edge_token` | 来源与刷新设置；token 单独保存 |
| 缓存／排期 | `.edge-page.bin`、`edge_next` | 一张页面、下次同步 UTC 时间 |
| 身份／同步 | `comp_identity`、`comp_sync`、BLE bonds | 授权与同步，不混入路由设置 |
| 运行状态 | snapshot、radio arbiter | 电量、忙状态、退避，不逐次写 NVS |
| 时间／睡眠 | Time、Platform、Power | 本机决定唤醒与休眠 |

- 路由先服从停止／关机，再检查用户模式、凭据、授权、电量和请求限制。
- `conn_policy` 保留既有 0–3 值；缺失默认自动。读取失败或非法值转离线并报错，不自动修复。
- 保存成功才更新设置；相同设置不重复写 flash。显式设置可修复非法值，但不能绕过读取故障。
- **离线只禁止资源路由，不会关闭 BLE。** 深睡时由 Platform / Power 关闭无线电。
- Wi-Fi 或传书忙时不能修改凭据。清除 Wi-Fi 不影响手机身份、bonds、同步数据或书籍。
- 状态不输出密码、token 或密钥；Wi-Fi 已配置状态不依赖 HTTPS 是否启用。
- 资源请求最低电量 20%，外接电源可放行；无效采样按 0% 处理。资源响应上限仍为 2048B。
- 密码不进入普通配置导出或日志；不新增明文 JSON 密码配置。

BLE Companion、NFC 配对、手机资源代理、直接 HTTPS、AP/STA 传书和 USB CLI 已有实现。
Hello 不代表授权；NFC 不是远程管理入口；应用不能选择任意 URL 或 GATT。
USB 不通过 BLE 传终端文本，TLS 校验不能关闭。
HA 状态读取与 MQTT discovery／上报由[服务端桥接](../tools/ha-bridge/README.md)提供。
BTHome 在已有定时唤醒时广播电量；默认关闭，不能替代页面下发或授权。

## 后台刷新与睡眠

相关设置保存在一个 152B 记录中，避免只保存半个来源；token 独立存储。

| 设置 | 默认 | 范围 |
| --- | --- | --- |
| 后台刷新／远程锁屏 | 关闭 | 开／关 |
| 同步间隔 | 6 小时 | 300–86400 秒 |
| 联网预算 | 15 秒 | 1000–60000 毫秒 |
| 最低电量 | 20% | 20–100% |
| 安静时段 | 关闭 | 本地午夜起 0–1439 分钟 |

安静时段起止相同表示关闭；1320–420 表示 22:00–07:00。
这些默认值不是续航测量结果。关闭后台不影响日历和阅读。

唤醒流程：检查时钟／电量 → 可选下载 → 保存并展示 → 合并日历、过期和同步时间 → 睡眠。

- 每次唤醒最多下载一次，不自动启动 BLE。按键可取消下载，等待有界无线清理后进入界面；渲染不联网。
- 低电量、离线策略和 OTA 确认流程优先于远程计划。
- 只接收已生效且未过期的页面，有效期最长 7 天，不排队未来页面。
- 服务端建议同步时间限制在至少 5 分钟后、本地正常周期前，并避开安静时段；失败按正常周期重试。
- 配置在下次睡眠／唤醒生效。墙钟用于日历，单调时间用于超时；校时后重新排期。
- 深睡保留电池锁存和定时器；服务器不能直接唤醒设备。
- 空白锁屏优先。缓存过期、来源改变、UTC 无效或读取／渲染失败时回退本地锁屏。
- 缓存仅一张 15032B 页面：staging → 验证 → flush/fsync → rename，不先删除旧文件。
- 同 revision 内容必须不变，不重复写入；同来源的旧 revision 不覆盖新缓存。来源变更递增 source generation，使旧缓存失效。

设备记录 `wake_after_us`，尚无服务器排期回执、传感计划或未来页面队列。
SPIFFS 断电恢复、实际无线关闭和续航仍待实测。

## 配置入口

设备“网络设置”支持路由、后台刷新（关闭／1h／6h／24h）、远程锁屏、BTHome 和清除 Wi-Fi。
“刷新设置”可调联网预算（5／15／30 秒）、电量门槛（20／40／60%）和安静时段（关闭／22:00–07:00）。
清除需确认；没有 HTTPS 来源时不能启用后台刷新。

USB 写操作需在 15 秒内执行返回的 `confirm <编号>`。含空格的参数加引号：

```text
connectivity status
connectivity policy automatic
connectivity wifi-set "SSID" "PASSPHRASE"
connectivity wifi-clear
connectivity source display.example.com /note4/page
connectivity token <32至64字符的专用只读token>
connectivity background 1 21600 15000 20 1320 420
connectivity remote-cover 1
connectivity bthome 1
```

- `connectivity token -` 清除 token。
- `connectivity background 0 21600 15000 20 0 0` 关闭后台刷新。
- 确认／状态输出不含密码或 token，但输入可能被终端录制或历史保存，请避免记录敏感命令。
- HTTP client 日志限为 INFO，防止 DEBUG 输出 Bearer 请求头；保留 INFO/WARN/ERROR 与 TLS 校验。
- 只使用页面 GET 专用 token，不使用 HA 管理员 token。
- 本次未启用 flash/NVS 加密，不保证抵抗物理 flash 读取。

## 页面格式与服务

专用页面走 HTTPS 443，校验证书、主机名和日期。禁止 query token、URL userinfo、
自定义端口、跳转、压缩和超长响应；要求 `Content-Type: application/octet-stream`。
不改变 Companion 资源接口的 2048B 上限。

页面为 32B 头部加 15000B 紧密打包像素，MSB-first，**1 白、0 黑**。

| 偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0 | 4 | `ZEP1` |
| 4 | 1 | 0 = 400×300；1 = 300×400 |
| 5 | 3 | 零 |
| 8 | 4 | 非零 revision |
| 12 | 4 | issued_at，UTC Unix 秒 |
| 16 | 4 | expires_at，UTC Unix 秒 |
| 20 | 4 | next_sync_at；0 使用本地周期 |
| 24 | 4 | 服务端为零，缓存写入 source generation |
| 28 | 4 | 零 |

32 位字段均为小端。生产服务须持久化递增 revision，不能因时钟回退重用版本。

[示例服务](../tools/edge-page-server.ts)：设置环境变量 `EDGE_TOKEN` 为 32–64 字符随机只读值，然后运行：

```sh
bun tools/edge-page-server.ts /path/to/rendered-page.pbm
```

服务监听 127.0.0.1:8787，需反向代理为设备可访问、证书受信任的 HTTPS 地址。
输入须为规范 `P4\n300 400\n` 或 `P4\n400 300\n` PBM；服务去掉行填充并转换黑白极性。
简单示例由外部进程更新 PBM；[HA bridge](../tools/ha-bridge/README.md)可直接读取实体并渲染中文页面。
设备下载时携带有效电量快照，桥接服务负责 MQTT 上报，不让设备维持 broker 连接。

## 验证

- `tools/test-connectivity-settings.sh`：默认值、兼容、配置故障、凭据保存／清除和唤醒排期。
- `tools/test-edge-display.sh`：下载预算、取消、计时回绕、清理失败、缓存替换，以及本地 HTTP 鉴权和像素转换。
- 现有路由、Wi-Fi、资源、传书和平台测试继续保留；固件构建验证真实链接。
- 电流、定时唤醒和断电恢复须连接设备实测，不能用主机测试代替。
- HA bridge 有独立测试；真实 HA／设备资格验证未完成，软件实现不等于部署完成。
