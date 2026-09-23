## 三十四、GK6323 瓦片行重排破案与 v5 实测（2026-09-23 晚）

### 结论

- §三十二记录的 14 FPS 和连续帧数只说明私有解码管线能运转；当时的“清晰度验收”已被用户报告和截图推翻。真正的断笔/横向错位来自 vdec 输出的 SP420 瓦片**行重排**，不是最近邻缩放本身。§三十三 v4 只修正色度纵向采样和失真保护，没有解开瓦片布局。
- GK6323 非压缩帧的亮度是宽 64 字节、高 16 行的瓦片；横向瓦片索引 `tx`、瓦片内逻辑行 `ty` 对应的物理行是 `ty ^ ((tx & 3) << 2)`。色度 VU 是宽 64 字节、高 8 行的瓦片，物理行是 `ty ^ ((tx & 1) << 2)`。瓦片网格宽度必须按 `stride/64`，不能按可见宽度。此公式适用于已测试的 1920×1080 和 2880×1800、`flag=0` 输出；4K `flag=0x20` 的压缩布局仍未知。
- `goke_daemon_v5_1800.c` 按上述公式直接提取 Y/VU，并用每行预计算的采样地址表处理 2880×1800→1728×1080。4K 压缩帧仍拒绝桥接，让官方 RustDesk 回退 VP9；不改 Mac 显示分辨率。

### 可重复的字节级证据

1. `diagnostics_v5/gen_pattern1080.swift` 生成带彩条与细字的 HEVC 合成流，`diagnostics_v5/pattern1080` 和 `pattern1800` 保存生成物；Mac `decode_nv12_ref.swift` 用 AVAssetReader 输出第 30 帧软件解码 Y/UV 参考。
2. 盒端 `goke_client_v4 hevc_pattern1080`：输入 90 个 AU，输出 88 帧，帧 30 的 1920×1080 NV21 与 Mac 参考逐字节比较：Y 0/2,073,600 字节不同，VU 0/1,036,800 字节不同（参考 UV 要交换成 NV21 的 VU）。
3. 盒端 `goke_client_v4 hevc_pattern1800`：输入 90 个 AU，输出 88 帧，帧 30 的 1728×1080 NV21 与 Mac 2880×1800 软件解码帧按同一采样坐标比较：Y 0/1,866,240 字节不同，VU 0/933,120 字节不同。该结果是在修复 MMZ 映射缓存满时总驱逐第 0 项的问题并重编译后再次取得。
4. 2880 合成流最初的逐瓦片扫描版 5 秒只输出 51/90 帧；预计算地址表版 5 秒输出 88/90 帧。暖机后一次 Y/VU 提取约 28–41 ms，socket 发送约 15–26 ms。此为合成流吞吐，不等同真实远控 FPS/CPU 验收。
5. 4K 合成流首帧报告 `unsupported output 3840x2160 flag=20`，输出 0 帧且守护进程继续监听；实验客户端写入时因对端主动关闭收到 SIGPIPE（退出码 141），属于预期保护路径。

### 当前设备状态与后续

- 盒端当前运行 `/data/local/tmp/goke_daemon_v5_1800_fixed`，Unix socket `/data/local/tmp/goke-vdec.sock`；源和 armv7 构建物在本目录。官方 Mac `/Applications/RustDesk.app` 签名验证正常，未替换。没有改 `/system`、`/vendor`、预装应用或 Mac 显示模式；没有重启盒子、force-stop 盒端 RustDesk。
- 真实远控测试在盒端连接后显示“已连接，等待画面传输”：Mac 官方 RustDesk 服务日志重复 `Failed to create capturer`（`CGDisplayStreamCreateWithDispatchQueue` 阶段）。macOS `tccd` 22:25:52 系统日志明确记载 `Failed to match existing code requirement for subject com.carriez.rustdesk and service kTCCServiceScreenCapture`，随后 `returning denied`；官方 app 的 Developer ID 签名本身通过 `codesign --verify --deep --strict`。因此当前阻塞是录屏授权记录与正在运行的应用身份不匹配，需在系统设置为**官方 RustDesk**重新授权；独立 RustDeskCSD.app 也缺新 bundle ID 的授权。不能把上述合成流结果冒充真实验收。外接屏当前亦未出现在 `system_profiler SPDisplaysDataType`，但它不解释内屏录屏被 TCC 明确拒绝。
- 录屏恢复后，以官方服务端内屏 2880×1800、盒端 v5 先做文字清晰度和动态图像测试；记录浮层 FPS、盒端 RustDesk CPU、持续 H265 无 VP9 回退。再测外屏 4K 时仍须走 VP9 保护或获得独立 CSD 授权后测试 Mac 编码前降采样。用户日常连接优先，实验结束要保证官方 server 可用。

### 构建

`$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/armv7a-linux-androideabi21-clang goke_daemon_v5_1800.c -o goke_daemon_v5_1800 -ldl -llog -O2 -pthread`

其中 NDK 本机路径为 `/Users/taoge/Library/Android/sdk/ndk/28.2.13676358`；源码只通过 `dlopen` 调用盒端厂商库。
