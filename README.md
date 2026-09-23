# CM311-5s (GK6323) RustDesk 硬件解码攻关仓库

中国移动 CM311-5s-ZG 机顶盒（国科微 GK6323V100C，armv7，Android 9）上让 RustDesk 远控走通硬件解码的完整攻关记录与源码。

## 目录结构

- `rustdesk/` — RustDesk 1.4.9 深度定制源码（基线 tag 1.4.9，含全部自研 patch）
  - `libs/scrap/src/common/goke.rs` — 私有 vdec 守护进程客户端（Unix socket → ANativeWindow）
  - `libs/scrap/src/common/mediacodec.rs` — CSD 参数集拆分 flag=2、Surface 暖机预算、configure 重试
  - `libs/scrap/src/common/codec.rs` — Android dispatch 遮蔽修复、goke 优先、VP8 屏蔽
  - `src/flutter_ffi.rs` / `flutter/lib/...` — 守护进程拉起、Surface 视图切换、首帧事件
- `goke-re/` — 厂商媒体栈逆向工具与产物（详见 goke-re/README.md）
- `build/` — 构建脚本（build_rd_hw.sh Rust .so、build_csd.sh 一键全量）
- `docs/` — 交接文档全量（NAS /vol1/1000/cmcc_box/交接文档.md 的镜像）

## 关键结论速查（详见 docs/交接文档.md §二十四～二十九）

1. 官方安卓 APK 全是软解；hard 解需 `mediacodec` 特性 + 三个自研 bug 修复（§二十七）
2. goke OMX/NDK MediaCodec 的输出是 **EOS/flush 门控**，实时会话拿不到帧（§二十七）
3. **私有 vdec 通道**（libgk_msp：GK_SYS_Init → AVPLAY_Init → Create → ChnOpen → mpi_vdec_chan_*）无 EOS 持续出帧，H264/HEVC 皆可（§二十八）
4. root 守护进程（goke-re/goke_daemon_v3.c）+ 映射缓存 + 融合采样读取：4K 流解码→720p 输出 **~15fps**
5. **4K 帧为芯片级强制压缩**（flag=0x20 + 熵编码码流），软件关不掉；需接 libgk_gfx2d（PadptGfx2dBitBlit 参照实现）硬件解压——进行中（§二十九）
6. Mac 端 H264 编码路线已证伪（hwcodec 上游故意关闭 + AVC 同被门控）

## 构建

```bash
# Rust .so（Mac，NDK r28c + vcpkg arm-android）
bash build/build_csd.sh
# Flutter APK
cd rustdesk/flutter && flutter build apk --release --target-platform android-arm
# 部署（Mac 不在盒子网段，经 fnnas 中转）
rsync -e ssh rustdesk/flutter/build/app/outputs/flutter-apk/app-release.apk \
  fn_tailscale:/vol1/1000/cmcc_box/
ssh fn_tailscale "adb -s 192.168.31.103:5555 install -r -d /vol1/1000/cmcc_box/app-release.apk"
```

## 铁律

- 禁卸载/禁用预装（ROM 有数据重置自愈机制）
- 禁 `adb reboot`（热重启缺陷 → 循环重启 + 数据重置）
- 禁 force-stop 盒子 RustDesk（Mac 端会留隐私模式僵尸会话）
