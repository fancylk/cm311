
## 二十九、4K 压缩帧破局进行时：性能 15fps 达成 + 压缩格式定性 + gfx2d 硬件通路定位（2026-09-23 晚，ZCode 会话，接 Codex §二十八）

> Codex 会话已突破私有 vdec 实时出帧并交付 root 守护进程 + RustDesk 接入（见 §二十八）。本节记录在其基础上的性能破局、4K 压缩帧定性，以及硬件解压通路的完整情报。**当前状态：≤1080p 流全链路可用；4K 流解码/传输 15fps 但像素数据为芯片级压缩格式，待接 gfx2d/VPSS 硬件解压。**

### 一、性能破局（守护进程 v3，源码 goke_re/round29.tgz）
- 修复 v3 初版崩溃：Api 结构体成员顺序与初始化器不一致（函数指针表错位导致 nv21 槽位=MMZ_Map，首帧转换必崩）——教训：dlopen 函数表务必逐槽核对
- **映射缓存**：每帧 mmz_map/unmap 8.4MB+4.2MB 开销 ~240ms；按物理地址缓存映射（DPB 复用同几个缓冲）后消失
- **MMZ_CACHED=1**：MPI_MMZ_Map(phys,1) 缓存映射，顺序读快 5 倍+。缓存一致性风险（DMA 写 vs CPU 缓存）待长跑验证
- **融合采样读取**：下采样时不再全平面去分块，按输出行直接定位 tile 内 64B 行采样，MMZ 读量 12.4MB→~3.2MB/帧
- 成绩：rgba_from_frame 290ms→**52ms**，4K 流解码+缩放至 720p 稳定 **~15fps**（60ms/帧），84/90 帧排空，分段计时 recv=0ms（硬解零压力）/rgba=52ms/send=15ms
- 健壮性：accept 后 SO_RCVTIMEO 3s（防幽灵连接卡死单线程 accept 环）、客户端 connect 重试；修复 vdec_test.c 的 recv_frame 字段名等
- 已知限制：AVPLAY_Create 的 video_buf 提到 64MB 会**挂死**（mmz 池不够），保持默认 16MB

### 二、4K 压缩帧定性（关键结论，别再走弯路）
- 4K（3840×2160）流解码输出帧带 flag=0x20 + Y 数据偏移 0x8800 / VU 偏移 0x4400：**压缩头 + 熵编码码流，不是纯平铺偏移**（实测合成纯色流仍为周期性条纹）
- **压缩是芯片级强制的**（4K DDR 带宽约束）：
  - vdec chan_attr 720p 与 4K 完全一致（`24 0 0 0 64 f 0 0...`），无压缩开关字段
  - mpi_vdec_set_chan_frm_pack_type：值 0 → 0x80120002 参数无效；其他值 ioctl **挂死**；get 恒 6。frm_pack_type 路线堵死
  - service.media.omx.* 属性、notrender 均无效
- 码流为熵编码（全缓冲最高熵 1.68bit/字节，出现 `e0 01 1e` 类 6 字节周期码组），纯软件解压不可行 → **必须走硬件**
- dump 样本：fnnas goke_re/round29.tgz（cmp_y.bin 16MB，含已知内容合成帧，可做解压格式的已知明文分析）

### 三、硬件解压通路情报（下一会话的主攻，两条路）
1. **libgk_gfx2d.so（首选，16KB 小库易逆向）**：
   - 导出 gk_gfx2d_open/close/compose/wait_done/get_capability + gfx2d_get_cmp_surface_size/stride（**"cmp surface"=压缩 surface，官方概念**）
   - get_cmp_surface_size 公式已解码：size = align512(0x1fe + H*32) + stride*H*2（参数语义待核）
   - gk_gfx2d_compose(handle, param, 0x10, out)：ioctl 0xc0d03600（载荷 208 字节）；param: +0=？、+4=图层数≤7、+8=输出数组指针、+0xc=必须 0x130；驱动经 dup() 返回 **dma-buf fd**（含 +0x5c 主 fd + N 个 stride 0x18c 的 fd）
2. **参照实现已定位**：/vendor/lib/libplayer.so 的 **PadptDecodeFrame**（0xefb4 附近）调用 getGfx2dDevice；相关符号 PadptGfx2dBitBlit / PadptHalToGfx2dFormat / PadptPlayerToGfx2dFormat；日志串 `set video output format, frmLayout=%d, compressMode=%d, bitDepth=%d`——厂商播放器的解码帧（含压缩）经 gfx2d BitBlit 后显示，参数填充代码就在 libplayer 里，反汇编区域已存 round29/padpt_gfx2d_region.txt + player_gfx_strings.txt
3. 备选：libgk_msp 内部 vpss_* 符号组（create_vpss/create_port/set_port_attr/recv_frm/release_frm）+ vdec_vpss_cmd 绑定命令——avplay 内部即 vdec→VPSS→VO，但结构体全未知

### 四、当前盒子与工程状态
- 盒上：csdfix APK（含 goke 私有解码器优先逻辑）+ goke_daemon_v3 **已停止**（4K 流会输出条纹帧；APK 自动回退 VP9 软解稳定显示）。恢复实验：`cd /data/local/tmp && setsid ./goke_daemon_v3 </dev/null > log 2>&1 &`
- 测试资产：hevc4k_*.bin（4K 合成流）在盒 /data/local/tmp/；hevc720_*.bin 为 720p 对照；goke_client_v3 带帧时间戳测量；conn_probe 连接探针
- ≤1080p 流：私有解码全链路（含 Surface 显示）已验证可用，13-15fps
- 未触碰：/system、/vendor、预装应用；未 reboot；未 force-stop RustDesk

### 五、下一步（按优先级）
1. 逆向 libplayer.so 的 PadptGfx2dBitBlit/PadptPlayerToGfx2dFormat 参数填充（参照实现），在守护进程中接入 gk_gfx2d_compose：压缩帧→gfx2d→未压缩输出→现有采样路径
2. 输出 dma-buf fd 的消费方式：mmap（/dev/dma_heap 或 their mmz import）或直接绑定 Surface
3. 若 gfx2d 输出直接可得小尺寸（硬件缩放）， RGBA 转换可移到输出侧或继续 libyuv
4. 长跑验证 MMZ_CACHED 一致性；实机验收 FPS/CPU/不回退
