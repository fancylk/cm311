# 攻关过程记录（会话级时间线）

> 2026-09-23 单日全记录。前置历史见 docs/交接文档.md §一～二十三（刷机、软解定位、硬解 APK、QoS 调优）。

## 会话 1（凌晨～早晨，Mac/Codex 前身会话）：Surface 直通改造
- 产出 hwsurface APK：MediaCodec→Surface 零拷贝架构、Dart Texture 渲染、私有 vdec 探测
- 结论遗留：goke HEVC Surface/缓冲均"无输出"（后证明观测被 bug 污染）

## 会话 2（下午，ZCode）：CSD 逆向破案 + 固件门控定性（§二十七）
1. NDK 演练程序（vdec_test.c）对照实验：合成 H264/HEVC 流、flag/参数集变体矩阵
2. 发现 RustDesk 三 bug：
   - codec.rs hwcodec 分支遮蔽 mediacodec 分支（"don't support h265!" 死代码）
   - CSD flag 语义错（NDK CODEC_CONFIG=2 不是 1）+ 首帧双重入队 + Surface 模式不喂 CSD
   - model.dart createVideoSurface 被注释
3. 修复后：H265+Surface configure 成功、CSD 被 goke 接受（84B flags=2）
4. 定性固件墙：无 EOS 实时喂帧 8s 零输出，EOS 后即出帧——EOS 门控
5. 排除路：MediaFormat vendor 键、系统属性、直调 OMX、Mac H264（hwcodec 故意关闭 + AVC 同门控）
6. 厂商栈逆向：组件白名单（CTS/Netflix/腾讯视频 strstr 表）、FastOutputMode(0x7f000008)、ChannelAttributes(0x7f000001)、caller name 机制

## 会话 3（下午～傍晚，Codex）：私有 vdec 实时出帧突破（§二十八）
1. 修正会话 2 演练程序两处 bug（AU0 跳过、索引误读），重证 NDK 路径 EOS 门控成立
2. 定位驱动 ioctl：LowDelay=0x400826b4、FirstFrameBypass=0xc00826e3
3. **突破**：GK_SYS_Init → AVPLAY_Init → GetDefaultConfig → Create → ChnOpen(2) → mpi_vdec_chan_start，绕过 avplay 播放检查直接驱动 vdec 通道——H264/HEVC 无 EOS 连续出帧（LowDelay 非必需）
4. 帧布局：Y 64×16 分块、VU 64×8 VU 交错，像素还原验证通过
5. root 守护进程（Unix socket 协议）+ RustDesk goke.rs 客户端 + Flutter Surface 显示，v2 APK 实机 H.265 通

## 会话 4（晚间，ZCode）：性能破局 + 4K 压缩定性 + gfx2d 定位（§二十九）
1. 修复 daemon v3 崩溃：Api 函数表初始化顺序错位（教训：dlopen 表逐槽核对）
2. 性能：MMZ 映射缓存 + MPI_MMZ_Map cached=1 + 融合采样读取 → rgba 290ms→52ms，4K→720p **~15fps**（recv=0ms/rgba=52ms/send=15ms）
3. 4K 压缩定性：flag=0x20、Y 头 0x8800/VU 头 0x4400、熵编码码流（max 熵 1.68bit）→ 芯片级强制（chan_attr 720p/4K 一致、set_pack_type 无效/挂死）
4. dump 已知明文压缩帧样本（cmp_y.bin 16MB）供后续格式分析
5. 硬件解压通路定位：libgk_gfx2d（cmp surface 概念 + compose ioctl 0xc0d03600 + dup 返回 dma-buf fd）；参照实现 = libplayer.so **PadptDecodeFrame/PadptGfx2dBitBlit**（`frmLayout/compressMode/bitDepth` 格式属性）
6. 中断插曲：远控测试中外接屏掉线（USB-C 链路问题，非软件），已恢复双屏
7. 现状：daemon 停用（4K 像素未解），APK 回退 VP9 软解稳定；≤1080p 流私有解码全链路可用

## 下一步
1. 逆向 PadptGfx2dBitBlit 参数填充 → daemon 接入 gk_gfx2d_compose（压缩帧→硬件解压/缩放→现有采样路径）
2. dma-buf fd 消费（mmap 或绑 Surface）
3. 实机验收：画质浮层 FPS≥12 稳定、盒子 CPU<50%、长播不回退 VP9
