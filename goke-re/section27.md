

## 二十七、CSD 喂参逆向破案：三个代码 bug 修复 + 固件实时输出门控定论（2026-09-23 下午，Mac 会话）

> 接续 §二十六续2 的主攻方向（逆向 goke 私有 CSD 喂参）。结论先行：**CSD 喂法问题已破解并修复，但固件在 MediaCodec 之下还有一道"实时输出门控"**——解码器 configure/CSD/喂帧全部正常，就是不吐帧，直到 EOS flush。验收目标（硬解 FPS≥12）在当前固件上通过标准 API 无解。

### 一、破案过程（NDK 演练程序对照实验，全部证据在盒 /data/local/tmp/ + Mac ~/rd_mc/goke_re/）
用 Swift+AVFoundation 生成 H264/HEVC 1280x720 测试流（gen_stream.swift，annexb 参数集分离导出），NDK 交叉编译 vdec_test.c 推到盒子直跑 MediaCodec，控制变量：
- **变体A**（参数集 flag=2 当 CSD + 全部帧正常喂）：✅出帧（首帧 ~1.9s，恰在 EOS 后）
- **变体B**（整个首包当 CSD，即 RustDesk 原行为）：✅出帧（参数集跨会话残留干扰，见下）
- **变体C**（完全不喂 CSD）：也"出帧"——但 AU0 里只有 SEI+IDR 无参数集，能解是因为 goke 服务端跨会话保留参数集（libgk_msp 字符串 `keep_spspps` 实锤），实验被污染
- **变体R**（RustDesk 式实时喂、不发 EOS）：❌ 8 秒零输出（HEVC 与 AVC、缓冲与 Surface 全部如此）
- **结论：goke 的 NDK MediaCodec 输出是 EOS/flush 门控的，实时会话永远等不到帧**。厂商播放器实时播放走的是 libgk_msp 的 avplay/nxplayer 私有管线，不经标准 OMX。

### 二、RustDesk 侧三个叠加 bug（本次全部修复）
1. **codec.rs dispatch 遮蔽（最致命）**：`handle_video_frame` 的 H264s/H265s 分支在 hwcodec 特性下排在 mediacodec 分支之前，Android 上 h265_ram 被前人 patch 置 None → 永远 `Err("don't support h265!")`，**mediacodec 解码分支是不可达死代码**。之前所有"MediaCodec 无输出"观测都含此 bug。修复：两个 hwcodec 分支加 `not(target_os = "android")`。
2. **CSD 喂入错误**：mediacodec.rs 用 `queue_input_buffer(..., 1)` 当 CODEC_CONFIG——NDK 语义里 1=KEY_FRAME，**2 才是 CODEC_CONFIG**（Java 层才是 1）！且首帧被双重入队、Surface 模式压根不喂 CSD。修复：annexb 参数集拆分（HEVC NAL 32/33/34，H264 7/8）→ flag=2 CSD（实测服务器流确实带内嵌 VPS/SPS/PPS，盒子日志 `csd queued: 84 bytes (flags=2)` + goke 接受）+ 两种模式都喂 + 不再重 Feed。
3. **createVideoSurface 被注释**：model.dart 里整段注释掉（前一会话误判固件不支持而禁用），Surface 管线从未激活。已恢复。
另加三个健壮性修复：configure 重试退避（goke 服务冷启动/切换格式后短暂 ErrorUnknown）；probe 解码器不再占用 Surface（一个 Surface 一个消费者）；Surface 暖机 3s 预算只烧一次（goke 首帧延迟 ~2s，原 100ms×3 帧就放弃太急）。

### 三、固件门控逆向发现（静态+动态，供后来者）
- goke OMX 组件（libOMX.goke.video.decoder.so，薄封装）→ libgk_msp.so（海思 MSP 血统：mpi_vdec/avplay/VPSS）→ /dev/gk_omxvdec
- **调用方白名单**：组件 SetCallerName 处理器用 3 张 strstr 表比对调用者包名：cts.media/media.cts/xts.media/video.cts/cts.videoperf/security.cts/r.playbacktests/media.gts/youtube.gts/exoplayer.gts（第一组，解锁 profile 放宽 flag ctx+0x284）；m.netflix.ninja/roid.youtube.tv/android.videos/com.ktcp.video/com.google.android.exoplayer2.demo/com.cmgame.gamehalltv（第二组）；白名单只影响 profile/功能标志
- **FastOutputMode**：扩展索引 0x7f000008，写 ctx+0x20c，处理器日志 `set_parameter: fast_output_mode == TURE`（原文拼错）——但组件内无人读该标志，仅上报给框架（goke 改过的 libstagefright 有 `set caller name`/`ChannelAttributes` 逻辑），真正门控在 vdec 通道属性（SetLowdelay/FirstFrameBypass，libgk_msp）
- **MediaFormat vendor 键路线已排除**：AOSP9 ACodec 要求 `vendor.` 前缀且名字必须在组件暴露列表（仅 goke.output-mode/video-peek-in-tunnel 家族）；`vendor.goke.fast-output-mode`、`vendor.app.compatibility.enhance`、`OMX.Goke.Param.Index.FastOutputMode` 键全部试过无效
- **属性开关排除**：service.media.omx.notrender / render / logcat（组件 init 打印 `setprop logcat:1 test:0 render:1 lowram 0`）设了重启 media.codec 也不解锁实时输出
- **直调 OMX 也被墙**：root dlopen libOMX_Core + 裸调组件，GetParameter 要求精简 68 字节结构体（非标准 72），SendCommand(Idle) 崩溃/Invalid Command——核心对组件有私有封装（omx_test.c 留档）
- 框架侧 libstagefright.so 带符号，被 goke 改过（media.mc.youtube 属性机制、port mode、ChannelAttributes）——后续若继续，方向=①逆向框架 ChannelAttributes 白名单策略并 patch /system/lib/libstagefright.so（有备份风险自担）②root 守护进程走 avplay 私有 API（libgk_msp 的 mpi_avplay_*，厂商播放器真实管线）——工作量天级

### 四、Mac 端 H264 备选方向：已证伪，别再投入
- hwcodec 0.7.1 在 mac.mm `checkVideoToolboxSupport` 硬编码 `*h264Encoder=0`（注释："disabled due to frequent reliability issues"），ffmpeg_ram/encode.rs 的 h264_videotoolbox 也被注释——**Mac 端 h264:false 是上游故意关闭，非能力缺失**
- 但本机验证 AVFoundation/VT 的 H264 硬编码本身没问题（gen_stream.swift 产物即证）
- **关键：即使打通 Mac H264，盒子 AVC 走 NDK MediaCodec 同样被 flush 门控（变体R 实测零输出）→ 此路不通**。除非先破固件门控

### 五、当前盒子状态与产物
- **盒上运行：rustdesk-1.4.9-armv7-hwsurface-csdfix.apk**（fnnas /vol1/1000/cmcc_box/，md5 头 89e76b06，13:11 install -r -d 部署，配置未动）：H265 优先协商（VP8 已上报不支持——VP8 attach 会毒化 Surface 导致 HEVC configure 永久 ErrorUnknown，13:11 实测）→ H265+Surface configure 成功（Surface 首消费者）→ CSD 接受 → 暖机 3s 无帧（固件门控）→ 回退 VP9 软解稳定显示。**比旧版 hwsurface 多花 3s 回退，但保持了全部修复以备固件解锁**
- 构建链：Mac `bash ~/rd_mc/build_csd.sh`（=build_rd_hw.sh + jniLibs + flutter apk，改动=libs/scrap/src/common/{codec,mediacodec}.rs + flutter/lib/models/model.dart）
- 研究工具已归档 fnnas /vol1/1000/cmcc_box/goke_re/：vdec_test.c（NDK MediaCodec 演练）、omx_test.c+omx_mini.h（直调 OMX）、gen_stream.swift（AVFoundation 测试流）、libOMX.goke.video.decoder.so/libgk_msp.so/libstagefright.so 及反汇编
- 会话实况（13:59 截屏 now_csdfix.png）：用户实际使用中，画质浮层 Codec VP9 / FPS 1（静态画面 QoS 正常降帧）/ Delay 22ms
- 铁律遵守：全程未卸载/禁用预装、未 reboot、未 force-stop 盒子 app（会话由 TCP 自然超时回收）；测试用 setprop 均为非持久属性

### 六、验收达成情况（诚实结论）
- ❌ FPS≥12 硬解：固件门控未破，标准路径无解（见§三）
- ❌ 不回退 VP9：同上；但当前会话"开局即 VP9 稳定"（非中途回退），QoS 15fps 机制健康
- ⚠️ CPU<50%：VP9 软解 15fps 约 45-50%（§二十五补 调优结论仍有效）
- 若接受"软解+Surface 常显"现状，回滚旧版 bffd2738 亦可；csdfix 版在功能上严格优于旧版（多了三个 bug 修复与 Surface 管线）
