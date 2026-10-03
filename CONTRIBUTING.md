# Contributing / 参与贡献

Focused fixes, documentation and reproducible bug reports are welcome.
欢迎范围清晰的修正、文档改进和可复现的问题报告。

## Report a problem / 报告问题

Include device/revision, source commit, ESP-IDF version, power source and the
smallest reproducer. Remove credentials, Wi-Fi names, MAC addresses and personal
data from logs. Report security issues through [SECURITY.md](SECURITY.md).

提供设备与硬件版本、源码提交、ESP-IDF 版本、供电方式和最短复现步骤。
日志须去除凭据、Wi-Fi 名称、MAC 地址和个人数据；安全问题按上面的专用入口报告。

## Change and verify / 修改与验证

Keep one purpose per PR and update the relevant [documentation](docs/README.md).
Do not commit credentials, device secrets, raw Flash backups, audio captures,
generated `sdkconfig` or builds. Preserve licenses and upstream attribution.

每个 PR 聚焦一个目的，同步更新相关文档；不提交凭据、设备密钥、Flash 备份、
录音、生成配置或构建产物，并保留许可证及来源说明。

Run checks appropriate to the change:

```bash
git diff --check
shellcheck tools/*.sh
tools/test-host.sh --suite ui        # Choose the affected group(s).
tools/test-host.sh --jobs 2          # Full host suite before integration.
```

For firmware changes, use the [qualified environment](docs/PREREQUISITES.md)
and `tools/build-firmware.sh --profile full`; test other affected profiles too.
For Android changes, run `tools/test-android-companion.sh`.
[CI](docs/CI.md) runs host, Android and firmware checks independently.

固件修改使用已验证环境构建 Full 及受影响的其他预设；Android 修改运行对应测试。
仅文档修改检查内容和链接即可，不需要为此构建或烧录设备。

Describe physical validation separately from simulation. Flash only an
explicitly identified black-and-white Note4 with its exact port, after backup.
Do not claim hardware qualification from host tests.

区分真机验证和模拟测试。烧录前确认黑白 Note4、准确串口并备份，
不要把主机测试结果当作硬件验收。
