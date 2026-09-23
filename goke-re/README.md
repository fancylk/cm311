# goke 厂商媒体栈逆向工具集

## 演练/生产程序
- `goke_daemon_v3.c` — root 解码守护进程（当前版）：私有 vdec + Unix socket + 映射缓存 + 融合采样。编译：
  `armv7a-linux-androideabi21-clang goke_daemon_v3.c -o goke_daemon_v3 -ldl -llog -O2`
  部署：adb push /data/local/tmp/ && `setsid ./goke_daemon_v3 </dev/null > log 2>&1 &`
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
