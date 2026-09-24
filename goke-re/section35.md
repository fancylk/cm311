## 三十五、次日设备状态与授权阻塞（2026-09-24 09:17）

- Mac 两块实体显示器均已被 `system_profiler SPDisplaysDataType` 识别：外接 E272CU-ZS 3840×2160（UI 1920×1080）和 MacBook 内屏 2560×1600（此前 RustDesk 捕获帧 2880×1800）。未改显示模式。§三十四“外屏未识别”只是当晚临时状态，现已恢复。
- macOS「隐私与安全性 → 录屏与系统录音」当前列表显示 `RustDeskCSD` 开、**无官方 `RustDesk` 项**。系统 `tccd` 日志在 9 月 23 日 22:25:52 明确拒绝 `com.carriez.rustdesk` 录屏，原因是现有代码要求不匹配。官方 `/Applications/RustDesk.app` 的 Developer ID 签名校验通过，CSD 是独立 `com.carriez.rustdeskcsd` 的 ad-hoc 签名。官方录屏授权需在系统设置重新添加；电脑操作工具对此安全敏感权限变更要求操作当时单独确认，已向用户发出确认请求，尚未得到答复。不要索要或在聊天中接收系统密码。
- 官方 GUI 进程正占用 `/tmp/RustDesk-501/ipc`，用户级 `com.carriez.RustDesk_server` launch agent 当前不运行；最近没有活跃远控服务会话。自建服务器 31116/31117 TCP 可达，日志里 `/api/heartbeat` 超时是另一个非核心 API 路径，不能误判为 RustDesk 中继断网。
- 盒子 9 月 24 日约 07:31 曾冷启动（非本次操作），令临时 root 守护进程消失。09:14 已重新启动 `/data/local/tmp/goke_daemon_v5_1800_fixed`，PID 24576，Unix socket `/data/local/tmp/goke-vdec.sock` 正在监听；盒端 RustDesk app 进程运行。没有执行 `adb reboot` 或 force-stop 盒端 RustDesk。
- 获得官方录屏授权后，应先验证 Mac 捕获可创建，再从盒端连接；双屏情况下官方主屏是 4K 压缩源，v5 会安全回退 VP9。要验收 v5 的 2880 内屏 H265，应通过 RustDesk 客户端的显示器切换功能选内屏，不更改 macOS 分辨率或主屏。另一个备选是利用已显示开启录屏权限的 CSD 版本测试编码前降采样，但切换双方同一 ID 的应用会影响远程服务，须在确认当前无活跃会话后进行并留好回滚。
