# goke 厂商媒体栈逆向工具集

## 演练/生产程序
- `goke_daemon_v5_1800.c` — 当前实验版：修复 GK6323 亮度/色度瓦片行重排，允许内屏 2880×1800 缩到 1728×1080；仍拒绝 `flag=0x20` 的 4K 压缩帧。`goke_daemon_v5_1800.armv7` 是同源盒端二进制。编译命令见 `section34.md`；实机动态远控验收仍待 Mac 采集恢复。
- `goke_daemon_v5.c` — 同一布局修复的保守版：只接收 ≤1920×1080，其他源回退 VP9。
- `goke_daemon_v3.c` — 早期融合采样版，瓦片行重排错误，勿用于画质验收。
- `diagnostics_v5/` — 合成字/彩条 HEVC 流及 Mac 软件解码参考生成工具；第 30 帧 Y/VU 字节比对结果见 `section34.md`。
- `goke_client_v3.c` — 测量客户端（帧时间戳/保存 RGBA）
- `vdec_test.c` — NDK MediaCodec 演练（EOS 门控实验）
- `omx_test.c` + `omx_mini.h` — 直调 OMX（最小 ABI 头）
- `conn_probe.c` — socket 连接探针
- `gen_stream4k.swift` — AVFoundation 硬编码测试流生成（annexb 参数集分离）
- `sweep2.sh` — pack_type 扫描脚本

## 测试流（hevc/h264 各一套：params+frames+index）
- hevc_*/h264_*：1280×720；hevc4k_*：3840×2160

## 逆向产物
- `libgk_msp.so` / `libOMX.goke.video.decoder.so` / `libstagefright.so` / `libplayer.so` / `libgk_gfx2d.so`（盒子原库）
- `gfx2d_thumb.txt` / `player_thumb.txt` — 全量 Thumb 反汇编
- `padpt_gfx2d_region.txt` — libplayer PadptDecodeFrame 的 gfx2d 调用区（下阶段参照实现）
- `player_gfx_strings.txt` — PadptGfx2dBitBlit/PadptHalToGfx2dFormat 等符号
- `gk4k_frame_30.png` — 4K 压缩帧直读的条纹样本（问题快照）
