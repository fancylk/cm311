
## 三十三、阶段快照与新会话交接（2026-09-23 22:30，ZCode 会话收官）

> 本节写给下一个会话：当前一切可用的确切状态、已知边界、以及建议的下一步。此前技术细节见 §二十七～三十二。

### 一、当前运行状态（本节写作时点，实测）
- **Mac**：官方 RustDesk 1.4.9 运行中（/Applications/RustDesk.app，Developer ID 签名，TCC 有效）——iPad/手机/盒子都能连。无任何代码改动
- **盒子**：csdfix APK（含 goke 私有解码器）+ goke_daemon_v3 运行中（/data/local/tmp/，NV21 协议版）
- **盒子→Mac 会话**：H.265 硬解工作正常（2880×1800 内屏流 → daemon 解码 → 1728×1080 输出），FPS 9.6-14、延迟 ~19ms、无 VP9 回退、电视上文字可读
- 对端配置（peers/206231137.toml）已调优：image_quality='custom'/80、codec-preference='h265'、custom-fps='15'、show_quality_monitor=true
- /Applications/RustDeskCSD.app = 补丁实验版（独立 bundle id com.carriez.rustdeskcsd），当前未运行

### 二、双应用并存（重要机制）
- 官方 RustDesk.app 与 RustDeskCSD.app **不可同时运行**（共享 Mac ID 206231137 与同一配置目录 ~/Library/Preferences/com.carriez.RustDesk，会互抢注册）
- 实验协议：退官方 → 开 RustDeskCSD → 跑 `RustDeskCSD.app/Contents/MacOS/RustDesk --server` → 盒子连；实验完反向
- CSD 已有独立 bundle id → TCC 独立授权条目；首次在新机器授权走"录屏与系统录音"面板（macOS 26 的屏幕录制改名）
- **日常远程用户不需要做任何事**：官方版无改动，iPad/手机/盒子照常

### 三、本会话（ZCode 续 Codex）成果清单
1. daemon 会话超时误杀修复（SO_RCVTIMEO 只该用于 hello；静止画面 >3s 无帧曾被掐断→管道断裂→H265 被标记不支持→VP9）
2. NV21 协议：daemon 发 Y/VU 裸平面（GRF2 头），app 端 NV21ToARGB+ARGBToABGR（scrap 的 yuv_ffi bindgen 已含）写 Surface——套接字负载 7.5MB→2.8MB/帧
3. daemon 输出上限 1280×800→1728×1080；对端画质 30%→80%
4. 验收达成：H265 硬解、FPS 14、延迟 19ms、文字可读、无回退（§三十二）
5. 定性 4K 压缩为芯片级强制 + gfx2d/PadptGfx2dBitBlit 通路情报（§二十九）
6. 紧急恢复流程实测两次（§三十一 + restore_official_procedure.md）
7. 全部源码/工具/文档入 cm311 仓库（commit 至 49e7d3b+）

### 四、已知边界与坑（新会话注意）
1. **4K 外接屏场景有 UX 坑**：若会话采集到 4K HiDPI 显示器（外接 E272CU-ZS 主屏时），流是 4K → 芯片强制压缩帧 → daemon 输出条纹画面（用户会看到花屏而不是优雅回退）。建议改进：daemon 检测 f[16]&0x20（压缩标志）时回发错误头，app 端 GokeDecoder 收到后标记 H265 不支持 → 自动优雅回退 VP9。内屏（2880×1800）与 1080p 场景无此问题
2. 外接屏睡眠会从显示器列表消失（采集必失败）；长时间测试前 `caffeinate -d -t 14400`
3. fps 波动 9.6-14：受内容/码率/QoS 影响；custom-fps 可试 20
4. CPU：daemon ~71% + app 峰值 86%（合计约 4 核 43%）；优化方向=daemon 采样循环 NEON 化、Mac 端 1080p 直编
5. peers toml 写坏（非法 TOML）会让 app 重置该文件（免密密码丢失，需重输一次 Mac 密码）；改配置前先 force-stop app
6. 恢复官方版的完整流程：restore_official_procedure.md（实测两次）
7. 两个 RustDesk 服务的保活：root LaunchDaemon /Library/LaunchDaemons/com.carriez.RustDesk_service.plist 只保活官方路径

### 五、新会话下一步（按优先级）
1. **daemon 加压缩帧检测+优雅回退**（上面的 UX 坑，纯盒子端小改）
2. fps 稳定 12+：daemon rgba 段耗时剖析（当前 64ms@4K→1280；1728 档实测见日志）、采样循环 NEON 化、或 image_quality 微调
3. 长播 30 分钟压测（MMZ cached 一致性、内存泄漏）
4. 4K 外接屏通路二选一：Mac 降采样 dylib 部署（TCC 一次性授权，用户在场）或 gfx2d 硬解压（逆向 PadptGfx2dBitBlit）
5. 全部完成后：结果更新 §三十三、归档、提交 cm311 仓库
