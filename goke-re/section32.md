
## 三十二、NV21 协议落地：1080p 硬解全链路验收达成（2026-09-23 晚，ZCode 会话续）

> 用户目标修正：不要求 4K，保证 1080p 可看 + 帧率尽量高 + 不干扰日常。双应用并存方案落地。

### 一、双应用并存（用户要求，已部署）
- /Applications/RustDesk.app = 官方版（Developer ID 签名，TCC 原授权有效）——日常/iPad/手机远程，**任何情况下优先保它**
- /Applications/RustDeskCSD.app = 补丁实验版（ad-hoc + 降采样 dylib + NV21 协议，显示名 "RustDesk CSD"）
- **不可同时运行**（同一 Mac ID 206231137，会互抢注册）。实验协议：退官方 → 开 CSD → CSD --server → 盒子连；实验后反向。CSD 首次使用需在"录屏与系统录音"（macOS 26 对屏幕录制的改名）里授权一次
- 坑：/Applications 已注册 app 受 App Management 保护，改 bundle 内容会被拒 → 必须"删除+重装"整包；同 bundle id 的两个 app 共享同一条 TCC 授权记录（按 bundle id 一对一），授权解析到路径在前的那个二进制 → **CSD 必须用独立 bundle id**（已改 com.carriez.rustdeskcsd；配置目录硬编码 com.carriez.RustDesk 不受影响，ID/密码/对端配置共享）

### 二、NV21 协议（本节核心优化，帧率 7.8→14）
模糊根因：daemon 输出 1280×800 RGBA（1.5 倍上电视=糊）+ 对端画质 30%（2.3Mbps）。修复：
1. daemon 输出上限 1280×800 → **1728×1080**（源 2880×1800 采样 60%，原生 1080p 级清晰度）
2. 对端配置恢复并调优：image_quality='custom'/80（原 low/30 是 §25 软解时代的遗留）、codec-preference='h265'、show_quality_monitor=true
3. **协议改 NV21**：daemon 不再做 YUV→RGBA（省 10ms），发 GRF2 头 {magic,w,h,ylen,vulen,stride} + Y/VU 裸平面（1728×1080 ≈ 2.8MB/帧，比 RGBA 省 63% 套接字传输）；app 端 goke.rs 用 scrap 的 bindgen 绑定 NV21ToARGB + ARGBToABGR（yuv_ffi.h convert_argb.h 全量生成，静态库已含）NEON 转换直接写 ANativeWindow
4. 踩坑：daemon 会话套接字的 3s SO_RCVTIMEO（为 hello 防幽灵加的）会误杀静止画面的正常会话（>3s 无帧=超时断开→管道断裂→H265 被标记不支持）→ hello 读完后必须清零超时

### 三、验收数据（20:46-21:32 多轮实测）
- **Codec H265 稳定（无 VP9 回退）、FPS 14（9.6-14 随画面动态）、Delay 19ms、码率 7.3Mbps、1728×1080 输出**
- 连续显示 2760+ 帧无断流；电视上文字可读（用户已确认可用）
- CPU：goke_daemon ~71%，flutter_hbb 峰值 ~86%（两进程合计约占 4 核 43%）；后续可优化（daemon 标量采样循环 NEON 化 / Mac 端 1080p 直编减少解码量）
- 对端免密配置（peers/206231137.toml）曾因损坏的 TOML 被 app 重置，已从 box_backup_0923 恢复+调优（注意：设备上没有 /vol1，恢复要经 fnnas 中转 adb push）

### 四、遗留与下一步
1. **4K 外接屏场景**：外接屏 HiDPI 帧缓冲 3840×2160 → 4K 档强制压缩帧（flag=0x20）→ 当前 daemon 出条纹 → 依赖 gfx2d 硬件解压（§二十九 情报）或 Mac 端降采样 dylib（已构建好，见 deploy_patched_mac.sh，需用户在场做一次性 TCC 授权）
2. fps 提升空间：daemon 采样循环 NEON 化；image_quality 已 80，可试 custom-fps 提到 20
3. 恢复官方版的完整步骤见 restore_official_procedure.md（本轮已实测两次）
