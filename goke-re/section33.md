## 三十三、纠正画质验收并保护日常远程（2026-09-23 晚，Codex 接 ZCode §三十二）

### 结论与证据

- §三十二的 H.265 14 FPS/2760 帧证明私有解码、NV21 与 Surface 管线能持续运行，但**不构成画质验收**。用户再次报告盒子画面糊、字体歪；盒子 1920×1080 截图可见大量横向重复和笔画断裂。关闭守护进程后，同一官方 Mac 会话回退 VP9，截图立即恢复清晰，故问题在硬解帧到画面的路径。
- 官方 Mac RustDesk 送来 2880×1800 HEVC；帧元数据宽高 2880×1800、stride 3328、flag=0。原始 MMZ 亮度数据按现有 64×16 瓦片展开后已出现笔画断裂，表明仅替换最近邻缩放不能保证修好。将 `MPI_MMZ_Map` 从 cached 改为 uncached 后，条带仍在且 `nv21_from_frame` 从约 80 ms 增至 700–1000 ms，排除简单的缓存过期解释。把瓦片行宽从 stride/64=52 改成可见宽/64=45 后画面明显更坏，报告 stride 不能直接忽略。测试版本均已撤回；含 Mac 私人画面的临时 dump 不归档。
- ZCode 的只读审查额外发现 `goke_daemon_v3.c` 色度缩放行使用 `sy=row*h/oh/2`，只读取上半幅 VU；应为 `sy=row*h/oh`。该错误影响色彩，但不能解释已在亮度原始数据中出现的断笔。v4 已修复。
- §三十的 Mac 编码前降采样代码存在于独立的 RustDeskCSD.app，但此前 CSD 启动时日志出现 IPC 被占用；实测切换后，CSD 连接只报 `Failed to create capturer`，没有产生低于 1080p 的帧。ZCode 改 CSD 的 bundle ID 为 `com.carriez.rustdeskcsd` 发生在用户上次录屏授权之后，旧 TCC 条目不适用。**降采样实机链路尚未验收**。官方 RustDesk.app 保持原签名与独立可用。

### v4 守护进程与验证

- `goke_daemon_v4.c`：最多输出 1920×1080 NV21；超过 1920×1080 或帧 `flag&0x20` 时关闭桥接，让客户端回退 VP9，防止 2880 或 4K 错画；修正 VU 行索引；每次会话结束解映射 MMZ 缓存。未动显示器分辨率、/system、/vendor、预装应用，也未重启盒子或 force-stop 盒子 RustDesk。
- 盒子合成流：HEVC 1920×1080 输入 90 AU，输出 88 帧；保存的第 30 帧 NV21 转 RGB 后，几何和彩色方块正确。3840×2160 输入 5 AU 后检测 `unsupported output 3840x2160 flag=20`，输出 0 帧并清理会话；daemon 继续监听。
- 官方 Mac 实际重连：输入 2 AU 后检测 `unsupported output 2880x1800 flag=0`，输出 0 帧；RustDesk 日志约 1.5 秒后出现 `create VP9 decoder success`，盒子画面恢复清晰。最终 v4 加入 MMZ 解映射后重测 1920×1080 合成流仍为 88/90 帧。
- 当下官方 Mac 服务端已由用户级 `com.carriez.RustDesk_server` launch agent 启动，盒子实际回退 VP9 且截图清晰。盒上 v4 守护进程正在监听，下一次官方 2880×1800 连接会触发保护性回退。

### 接下来

1. 用户在 macOS「隐私与安全性 → 录屏与系统录音」给当前独立 bundle ID 的 RustDesk CSD 重新授权。切换时先确保官方用户级 server 停止并释放 `/tmp/RustDesk-501/ipc`，然后仅启动 CSD server；验证日志中采集器可创建，盒子首帧 `source` 必须 ≤1920×1080。
2. 用真实动态图像复验 H.265、FPS≥12 稳定、盒子 RustDesk 进程 CPU<50%、连续播放不回退 VP9，并检查字体清晰度。完成后恢复官方 server，确保 iPad/手机远程可用。
3. 如仍有错画，先把独立合成的文字/棋盘格 HEVC 流在盒端与软件解码参考逐像素比较，再逆向 vdec/VPSS 帧布局；仅凭 88/90 帧计数不足以宣称更高分辨率画质正确。
