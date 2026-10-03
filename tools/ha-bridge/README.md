# Note4 HA bridge

设备定时醒来，按预算获取 HTTPS 页面，再睡眠。服务器读取 HA 状态、渲染中文页面，
并把请求中的电量上报给 MQTT。设备不常驻 MQTT，也不持有 HA 账号或 broker 密码。

## 启动

```sh
bun install --cwd tools/ha-bridge --frozen-lockfile
# 通过服务管理器或受保护的环境文件设置下列变量，不提交凭据。
bun tools/ha-bridge/server.ts
```

必需变量：

| 变量 | 用途 |
| --- | --- |
| `HA_URL` | HA 根地址 |
| `HA_TOKEN` | 服务端 HA API token，不是网页登录密码 |
| `HA_ENTITIES` | 逗号分隔的 1–8 个实体 ID |
| `EDGE_TOKEN` | 设备专用只读 token，32–64 位字母、数字、下划线或短横线 |
| `MQTT_URL` | broker 地址，默认要求 `mqtts://` |
| `NOTE4_DEVICE_ID` | 唯一设备 ID，1–32 位小写字母、数字、下划线或短横线 |

可选：`MQTT_USERNAME`、`MQTT_PASSWORD`、`EDGE_PORT`（8787）、`EDGE_DB_PATH`、
`EDGE_ORIENTATION`（默认 `portrait`，可选 `landscape`）。
仅在明确接受受信任局域网明文通信时设置 `HA_ALLOW_HTTP=1` 或 `MQTT_ALLOW_INSECURE=1`。
不要将凭据嵌入 URL。建议 broker 使用专用账号和限定主题的 ACL。

服务仅监听 127.0.0.1，需反向代理为设备可访问的 HTTPS 443 地址；不得关闭设备 TLS 校验。
HA 请求总预算 3 秒，单实体响应上限 16KiB。MQTT 上报最多 2 秒，失败不阻止页面返回。
SQLite 保存递增 revision，重启和时钟回退不会重用版本；请保留数据库。
页面使用现有 OFL GNU Unifont 字体，最多 8 个实体；生成时间为 UTC。
竖屏原生 300×400 单列，横屏原生 400×300 双列；不是旋转或缩放竖屏图片。
方向随页面下发，不改变应用方向或本地日历的方向设置。

## 设备配置

```text
connectivity source display.example.com /note4/page
connectivity token <专用页面token>
connectivity background 1 21600 15000 20 0 0
connectivity remote-cover 1
connectivity bthome 1
```

写操作需在 15 秒内确认。网络设置页支持后台周期、联网预算、电量门槛、安静时段、
远程锁屏和 BTHome 开关。自定义来源和凭据由 USB 配置，不开放未经授权的远程写接口。

## HA 与 MQTT

HA 配置 MQTT integration 后，通过 retained discovery 自动发现电量、电压、充电和
“提供的页面版本”四个实体。后者不代表设备已经保存或显示页面。
状态写入 `note4/<id>/state`，discovery 写入 `homeassistant/.../config`。
QoS1，状态有效期为两次本地周期加 60 秒；保留消息中的采样时间用于拒绝过期数据，不能据此判断设备常在线。
设备快照在下载前采集；无效电池值不伪装成 0%。修改设备 ID 后，旧 discovery 需单独清理。

BTHome 使用 HA 内置集成，需本机蓝牙或 Bluetooth proxy。广播仅包含电量、电压和充电状态，
不含凭据、不接收命令、不加密；默认关闭。它与 MQTT 实体独立，不自动合并。
只在已有定时唤醒时发一次短广播，不新增独立唤醒周期；未知时钟、安静时段或电量不足会跳过。
广播启动／发送最多 3 秒，包含清理的调用等待最多 6 秒。清理超时后仍进入最终关机，
不进入应用或重启 BLE；未完成的私有任务由深睡终止。

## 验证

```sh
tools/test-ha-bridge.sh
# 显式实测 broker；只使用随机、非保留测试主题，不创建 HA 实体。
MQTT_TEST_URL=mqtt://broker:1883 bun tools/ha-bridge/mqtt-smoke.ts
```

本地测试覆盖 HA 鉴权、响应限制、中文渲染、版本递增、discovery、遥测和失败回退。
真实 HA API、设备广播、HTTPS、定时唤醒与功耗仍需部署／硬件验证。

协议资料：[BTHome v2](https://bthome.io/format/)、[HA REST API](https://developers.home-assistant.io/docs/api/rest/)、[MQTT discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)。
