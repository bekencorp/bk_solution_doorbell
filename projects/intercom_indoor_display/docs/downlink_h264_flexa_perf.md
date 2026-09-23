# intercom_indoor_display 下行 H.264 Flexa 解码性能报告

| 项 | 内容 |
| --- | --- |
| 工程 | `bk_solution_doorbell_release_4.0.1/projects/intercom_indoor_display` |
| 平台 | BK7259，AP + CP |
| 测试场景 | 室内屏接听视频通话，纯下行（本机上行关闭） |
| 码流 | 1280×720 H.264，App 协商 24fps，`pFrameCount=29` |
| 处理路径 | H.264 Flexa 解码 → GPU Flexa Bond（旋转 90° + 压缩 ARGB8888）→ OSD → DPU/MIPI 720×1280 |
| 测量时间 | 2026-09-11 17:51–17:52 |
| 结论摘要 | 单帧处理地板 **33.4ms**、常态 **36–40ms**；LCD 不是瓶颈；稳态可咬住 24fps，但无余量；起步 wait-IDR 与 Flexa 环过浅会造成出画掉到个位数～十几 fps |

---

## 1. 背景与测量目的

室内屏视频通话时下行出画帧率偏低。当前下行使用 Flexa 模式：

- 解码器按 **16 像素行** 写入 HSRAM 环形缓冲；
- 通过 `bk_flexa_h264d_gpu_bond` 与 GPU 绑核，GPU 按段旋转、压缩后交给 DPU。

本报告统计 **处理一帧的墙钟时间**，并对照 WiFi 入帧、解码成功/skip、GPU 出帧、LCD 回压，定位帧率瓶颈。

---

## 2. 下行处理链路

接听后拆除 LVGL，媒体通路为纯下行（`CONFIG_DOOR_ACCESS_CALL_LOCAL_UPLINK` 未开，PIP 关闭）。

```text
WiFi H.264 AU（零拷贝进 6×460KB 槽）
    → db_h264d：bk_h264_decode_frame()
        → VCDEC Flexa 写 NV12 环
           环深 seg=4，段高 16 行 → 64 行 / ~120KB HSRAM
        → Bond：每写出一段通知 GPU
    → GPU Flexa worker
        720/16 = 45 块
        每块：vg_lite_blit(NV12 → 压缩 ARGB, rotate=90) + finish + HPDMA
    → 帧尾 OSD（门锁 / 挂断 / 计时，osd_render_per_flexa_block=false）
    → DPU 解压刷新 MIPI 720×1280
```

`bk_h264_decode_frame()` 会等到 GPU 把本帧 Flexa 环消费完才返回。解码与 GPU 按段流水，环只有 4 段，GPU 慢时解码器堵在环上。因此：

**`decode_frame` 墙钟时间 ≈ max(硬解, GPU) + 排空 ≈ GPU 出一帧时间。**

本次启动日志确认：

- `DL HSRAM: seg=4 pp=122880 ping=41024`
- `downlink video started, 1280x720 fps=24 seg=4 slots=6 slot_cap=460800`
- `compositor start: main=1280x720 dst=1280x720 deg=90 pip=0`
- `vg_lite contiguous mem size: 0x14000`

---

## 3. 测量方法

在 `doorbell_downlink_video.c` / `doorbell_display_compositor.c` 用 `bk_aon_rtc_get_us()` 累计，通话中每 **5s** 打一行。

| 字段 | 含义 |
| --- | --- |
| `wifi[Nf kbps]` | 5s 内完整 AU 数与码率 |
| `h264d[ok / fail / skip]` | 解码成功 / 硬件失败 / wait-IDR 丢弃 |
| `h264d+gpu_wait` | 一次 `bk_h264_decode_frame()` 耗时（处理一帧） |
| `ivl`（解码侧） | 相邻两次送解的间隔 |
| `gpu_out ivl` | GPU 相邻 `frame_done` 间隔（实际上屏周期） |
| `lcd_wait` | DPU 双缓冲满时 `flush` 前等待 |

日志 TAG：`db-dl-vid`、`db-comp`。MONITOR 的 `GPU[Nf]` / `DPU[fps]` 作交叉验证。

---

## 4. 单帧处理耗时（核心数据）

统计窗口为接听后约 5s 一段，共 6 段。

| 窗口（约 5s） | WiFi 入帧 | 解码 ok / fail / skip | h264d+gpu_wait min/avg/max (µs) | 解码间隔 avg (µs) | GPU 出帧间隔 avg (µs) | lcd_wait avg (µs) | 折合出帧 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 起步 | 47（~2083kbps） | 7 / 0 / 40 | 33625 / **38727** / 51812 | 144053 | 165067 | 18 | ~6.9 fps |
| 2 | 94（~4543kbps） | 22 / 0 / 72 | 33438 / **36299** / 39343 | 193073 | 202502 | 7 | ~5.1 fps |
| 3 | 145（~6486kbps） | 52 / 0 / 90 | 33406 / **37463** / 39812 | 77694 | 79324 | 6 | ~12.8 fps |
| 4 对齐 | 115（~5177kbps） | 116 / 0 / 0 | 33407 / **40123** / 46219 | 42722 | 43153 | 4 | **23.4 fps** |
| 5 最好 | 118（~4552kbps） | 119 / 0 / 0 | 33406 / **39477** / 44594 | 41556 | 41886 | 5 | **24.0 fps** |
| 6 回落 | 109（~4550kbps） | 85 / 0 / 22 | 33531 / **39146** / 45687 | 58081 | 58768 | 8 | ~17.2 fps |

对应原始 log：

```text
cost: h264d+gpu_wait n=7   [33625/38727/51812]us ivl[34468/144053/771469]us ~6.9fps
cost: gpu_out n=6 ivl[35531/165067/771532]us lcd_wait n=5 [0/18/32]us

cost: h264d+gpu_wait n=22  [33438/36299/39343]us ivl[33500/193073/1414687]us ~5.1fps
cost: gpu_out n=21 ivl[34938/202502/1414844]us lcd_wait n=22 [0/7/32]us

cost: h264d+gpu_wait n=52  [33406/37463/39812]us ivl[33907/77694/460188]us ~12.8fps
cost: gpu_out n=51 ivl[38500/79324/460219]us lcd_wait n=52 [0/6/32]us

cost: h264d+gpu_wait n=116 [33407/40123/46219]us ivl[33687/42722/141563]us ~23.4fps
cost: gpu_out n=115 ivl[33938/43153/134625]us lcd_wait n=116 [0/4/32]us

cost: h264d+gpu_wait n=119 [33406/39477/44594]us ivl[33469/41556/130312]us ~24.0fps
cost: gpu_out n=118 ivl[35250/41886/130406]us lcd_wait n=119 [0/5/32]us

cost: h264d+gpu_wait n=85  [33531/39146/45687]us ivl[33625/58081/565375]us ~17.2fps
cost: gpu_out n=84 ivl[34906/58768/565719]us lcd_wait n=85 [0/8/32]us
```

### 4.1 汇总

| 指标 | 数值 | 说明 |
| --- | --- | --- |
| 单帧处理地板 | **33.4 ms** | 各窗口 min 均落在 33406–33625µs，硬件下限稳定 |
| 单帧处理常态 | **36–40 ms** | 成功解码帧的 avg |
| 单帧处理尖峰 | **45–52 ms** | 起步/resync 窗口 max |
| 理论天花板 | 1000/33.4 ≈ **30 fps**；按 40ms ≈ **25 fps** | 与协商 24fps 贴边，无余量 |
| LCD 回压 | **0–32 µs** | 可忽略 |
| DPU | `rps=36` 稳定 | 刷新能力足够，不是瓶颈 |

`h264d+gpu_wait` 与 `gpu_out ivl` 在稳态几乎相等，说明墙钟时间就是 GPU 在工作，解码器在等环。

按 45 个 16 行块折算：

- 33.4ms / 45 ≈ **0.74 ms/块**（1280×720 NV12 → 压缩 ARGB 旋转 90° 的 GPU 地板）
- avg 比 min 多 3–7ms：帧尾 OSD、调度、等环/resync

---

## 5. 出画帧率与 MONITOR 交叉验证

| 阶段 | MONITOR GPU / DPU | dpu_core layer0 | 与 cost 是否一致 |
| --- | --- | --- | --- |
| 接听后 ~3s | GPU[0–4f] DPU[fps 0–5] | 1.2–4.4 | 大量 skip，一致 |
| 对齐后 | GPU[23–24f] DPU[fps 23–24 rps 36] | 23.4–23.8 | 与 23.4/24.0fps 窗口一致 |
| 回落 | GPU[11–20f] DPU[fps 11–20] | 17.2 | 与 skip=22 窗口一致 |

稳态最好窗口 WiFi 约 23–24fps（118 帧 / 5s），解码成功 119，GPU/DPU 24fps，**处理能力刚好跟上协商帧率**。

---

## 6. 观感帧率低于 24 的原因

单帧 40ms 本身够咬 24fps，实际掉帧来自处理之外的丢弃与失步。

### 6.1 wait-IDR skip（解码 fail 始终为 0）

| 窗口 | skip | 含义 |
| --- | --- | --- |
| 1–3 | 40 + 72 + 90 | 从 GOP 中途进流，非 IDR 全部丢掉，前十几秒几乎不出画 |
| 4–5 | 0 | 参考链对齐，入帧 ≈ 出帧 |
| 6 | 22 | 槽位丢帧触发 `ref_break`，再次等 IDR；间隔 max 到 565ms |

伴随大量：

```text
doorbell.notify.requestKeyFrame  reason=frameLoss
```

这不是 VCDEC 解失败（`fail=0`），而是策略性丢 P 帧。

### 6.2 Flexa 环过浅，GPU 赶不上帧头

稳态 24fps 窗口仍持续出现：

```text
gpu_flex_main_entry, flexa sync from early block, frame N line 2/3
```

含义：上一帧 `frame_done`（HPDMA + OSD）未结束，解码器已把下一帧第 2、3 段写入环。环深仅 **4×16=64 行**，一帧 45 段；GPU 晚起步 2–3 段后余量只剩 1 段，容易 overrun，再触发丢帧/要关键帧。

---

## 7. 结论

1. **处理一帧（H.264 Flexa + GPU Bond）地板 33.4ms，常态 36–40ms，LCD 可忽略。**
2. **瓶颈在 GPU Flexa 旋转压缩（含帧尾 OSD），不在解码器、不在 DPU。**
3. 协商 24fps 与 40ms/帧贴边：对齐后能到 23–24fps，没有余量。
4. 若产品要 **稳定 24fps**：单帧时间够，需减少起步 skip 和环深=4 的 resync。
5. 若产品要 **30fps**：当前路径物理不够（地板 33ms）。

---

## 8. 后续优化方向

按收益大致排序：

1. **【已实施】开启下行 compositor GPU 输出池的 PSRAM 可覆盖写（write-through / cover-write）** —— 见第 10 节。
2. 减轻 GPU 帧尾 OSD，或改为按 Flexa 块绘制，缩短帧与帧之间的空窗。
3. 评估加深 Flexa 环（HSRAM 允许的前提下），降低 `sync from early block`。
4. 收紧 wait-IDR / 槽位丢帧后的关键帧请求，避免把偶发迟到放大成数百毫秒黑屏。
5. 仅在明确要求 30fps 时改 GPU 路径（降低旋转工作量、分辨率或换模式）。

---

## 10. PSRAM 可覆盖写（write-through / cover-write）优化

### 10.1 机制

BK7259 的 PSRAM 控制器支持给最多 **8 个区域（每颗 PSRAM 4 个）** 配置「可覆盖写 / 写透」（寄存器 `cover_start`/`cover_stop`）。开启后，落在该地址区间内的写请求由控制器按整块**直接覆盖写**，跳过部分 burst 的 read-modify-write（RMW），对「整帧写一次、DPU 读一次」的大缓冲，写带宽近乎翻倍；同时避免了 cache 维护开销。SDK 侧封装为帧缓冲 flag：

```c
bk_frame_buffer_set(frame, BK_FRAME_BUFFER_FLAG_WRITE_THROUGH);
```

`bk_mem_slab.c` 会为该帧分配一个 write-through 通道并调用 `bk_psram_enable_write_through(area, start, end)`；`bk_frame_buffer_free()` 时自动关闭并归还通道。

### 10.2 现状分析

- `CONFIG_PSRAM_WRITE_THROUGH=y` 在 SDK defconfig（AP/CP）**本来就开着**，8 个通道可用。
- 单画面预览路径 `components/multimedia_device_service/src/app_gpu.c::app_gpu_frame_malloc()` **已经**给 GPU 输出帧开了写透。
- 但室内屏视频通话走的是 **下行 compositor**（`doorbell_display_compositor.c`），其 3 个 GPU 输出池 buffer（`comp_out_pool_init()`）**此前只是普通 cached PSRAM，没开写透** —— 通话期间 8 个通道一个都没用上。

下行每帧的主开销正是 **GPU 把整帧压缩 ARGB 写进 PSRAM 池**（NV12 从 HSRAM Flexa 环读入，压缩 ARGB 写到 PSRAM），恰好是可覆盖写能加速的写路径。

### 10.3 改动

`components/smart_intercom/src/doorbell_display_compositor.c` 的 `comp_out_pool_init()`：每个池 buffer 分配后加

```c
#if (CONFIG_PSRAM_WRITE_THROUGH)
    if (bk_frame_buffer_set(s_comp.out_pool[i].buf,
                            BK_FRAME_BUFFER_FLAG_WRITE_THROUGH) != BK_OK)
    {
        LOGW("out_pool[%u] write-through enable failed, stay cached\n", i);
    }
#endif
```

- 池 buffer 存活整个通话，占用 3/8 个通道；通话独占显示时其余通道空闲，不冲突。
- 失败非致命：退回普通 cached，功能不受影响。
- 帧尾临时兜底 buffer（`comp_out_alloc()` 里的 malloc）仍保持普通 cached，不浪费通道。

### 10.4 验证与测试

- 编译：已通过。
- 板级实测结论：**无效**。稳态（`skip=0`）`h264d+gpu_wait` 地板 32.8ms、avg 39.0–39.7ms、max 42–44ms，与基线（地板 33.4ms、常态 36–40ms）几乎重合；`gpu_out` ivl avg 42–44ms、fps 22–24，均未改善。

### 10.5 为什么无效

- GPU 卡在 VG-Lite 旋转+压缩的**计算**上，`decode_frame` 墙上时间等的是 GPU 算完，不是等 PSRAM 写完 → cover-write 省的 RMW 不在关键路径。
- 输出是压缩 ARGB，写入量小，且 HPDMA 整帧写多为对齐整 burst，本就少触发 RMW。
- 更深层原因见第 11 节：GPU 输出与解码器帧共用同一颗 PSRAM0，瓶颈更像是**带宽争用**而非单路写效率，cover-write 减不了争用。

---

## 11. PSRAM0/PSRAM1 读写分离（降低带宽争用）

### 11.1 内存映射（本工程，非交织）

`build/.../partitions/ram_regions.h` + `reg_base.h`（`CONFIG_PSRAM_INTERLEAVE` 未开）：

| 用途 | 堆 | 地址 | 芯片 |
|---|---|---|---|
| H264 解码器 recon/参考/DPB/输出 + vcdec 内部缓冲 | `MEM_SLAB_HEAP_UNCODED` | `0x60000000`,16MB | **PSRAM0** |
| GPU 输出帧（compositor 压缩 ARGB）| `MEM_SLAB_HEAP_UNCODED` | `0x60000000` | **PSRAM0** |
| H264 输入码流 slot（下行 6×460800）| `MEM_SLAB_HEAP_CODED` | `0x64000000`,~12.9MB | PSRAM1 |
| 通用 psram_malloc 堆 / code / data | psram heap | `0x64CA7000`+ | PSRAM1 |

（GPU tessellation/contiguous mem `0x2c…` 和 Flexa 16 行环在 HSRAM，不占 PSRAM。）

问题：解码器 recon 写 + 参考读 + **GPU ARGB 写 + DPU 扫描读** 全压在 PSRAM0，一帧内四路流量争抢同一控制器带宽；PSRAM1 相对空闲。

### 11.2 改动

`doorbell_display_compositor.c`：新增 `COMP_OUT_HEAP = MEM_SLAB_HEAP_CODED`，把主 GPU 输出池（`comp_out_pool_init`）和其临时兜底（`comp_out_alloc`）从 UNCODED(PSRAM0) 改到 CODED(PSRAM1)。解码器帧仍在 PSRAM0，PIP 自视图仍在 PSRAM0。

分离后：
- **PSRAM0**：解码器 recon 写 + 参考读（解码工作量）
- **PSRAM1**：输入码流（轻）+ GPU ARGB 写 + DPU 扫描读（GPU+显示工作量）

容量核对：CODED 12.9MB = 下行 slot 2.76MB + GPU 输出池 3×~900KB(2.7MB) + 兜底 ≈ 5.5MB，余量充足；uplink 关闭，CODED 无编码器占用。

### 11.3 验证与测试

- 编译：已通过（`build/bk7259/intercom_indoor_display/package/all-app.bin`）。
- 板级测试（方法同第 9 节）：对比稳态（`skip=0`）
  - `db-dl-vid: cost: h264d+gpu_wait` min/avg/max
  - `db-comp: cost: gpu_out ivl` avg
  - `db-dl-vid: fps:`

  预期：GPU 写 + DPU 读移出 PSRAM0 后与解码器不再抢带宽，`gpu_out` ivl avg 与 `h264d+gpu_wait` avg 下降、fps 上升。对照基线：地板 33.4ms、常态 36–40ms（第 5 节）与写透实测（10.4）。若仍无改善，则说明瓶颈纯在 GPU 计算，需转向第 8 节计算侧优化。

---

## 9. 复测方法

编译烧录带 `cost:` 统计的固件，接听后抓 AP log，过滤：

```text
db-dl-vid: fps:
db-dl-vid: cost:
db-comp: cost:
MONITOR: GPU[
flexa sync from early block
```

关注稳态窗口（`skip=0`）的 `h264d+gpu_wait` avg，以及是否仍有 `sync from early block`。

---

## 12. 性能优化基准（GOP 模拟，固定复用）

后续所有下行解码显示性能优化，都以本节数据作为对照基线。测试条件固定不变：

- 测试宏 `DL_TEST_LOOP_GOP=1`（`doorbell_downlink_video.c`），屏蔽 WiFi 收包抖动/丢帧。
- 缓存 1 帧 IDR + 1 帧 P，循环 `1 I + 29 P` 模拟真实 GOP。
- **P 帧画面几乎不动**（低运动矢量、小残差），AU 体积小且稳定。
- 已应用的优化：PSRAM0/1 分离（第 11 节）+ Flexa 环搬到 SRAM5/6（禁 USB + seg=3）。
- 面板 720×1280，DPU rps 上限 36Hz。

### 12.1 基准数据（44190 / 49190 两窗口一致）

| 指标 | min / avg / max | 说明 |
|---|---|---|
| **I 帧** `h264d+gpu_wait` | 27.3 / **27.9** / 29.0 ms | 纯 intra 重建，无参考帧读 |
| **P 帧** `h264d+gpu_wait` | 30.2 / **30.6** / 32.6 ms | 含 PSRAM 参考帧读（~1.38MB）+ 运动补偿 |
| 混合(1I+29P) `h264d+gpu_wait` | 27.3 / **30.5** / 32.6 ms | **~32.9 fps** |
| `db-comp gpu_out` ivl | 27.3 / **30.5** / 32.7 ms | GPU 出帧节奏，与解码一致 |
| `lcd_wait` | 0 / 9 / 32 µs | DPU 不等待，非瓶颈 |
| GPU / DPU (MONITOR) | 32–33 fps / 32–33 fps | rps 36（面板上限） |

I/P 帧固定差 **~2.7ms**，即 P 帧多出的 PSRAM 参考帧读 + 运动补偿开销（画面几乎不动时最小）。

### 12.3 A/B 验证：FLEXA 环 HSRAM vs SRAM5/6

用 `DL_FLEXA_RING_ON_SRAM` 开关（`doorbell_downlink_video.c`）做对照，其它条件全部不变
（GOP 模拟、近静止 P 帧、PSRAM0/1 分离、seg=3、禁 USB），只改 FLEXA 环的存放 bank：

| 指标 | HSRAM (SRAM3/4) `=0` | **SRAM5/6** `=1` | SRAM 收益 |
|---|---|---|---|
| I 帧 avg | 29.7 ms | **27.9 ms** | −1.8 ms |
| P 帧 avg | 32.68 ms | **30.6 ms** | −2.1 ms |
| 混合 fps | ~30.85 fps | **~32.9 fps** | **+2.0 fps (~6.6%)** |
| `lcd_wait` | 0/8/62 µs | 0/9/32 µs | 一致（DPU 不等） |

关键证据：HSRAM 复测时 **P 帧 AU 仅 704B**（比 SRAM 基准 3830B 更静止），理应更快，
实测却**慢 2.1ms** —— 差异只能来自 FLEXA 环从 SRAM5/6 挪回 HSRAM，**干净证明 SRAM5/6 有效**。
原因：HSRAM(SRAM3/4) 与 GPU ping-pong 输出 / VG-Lite contiguous / DPU flush 接口共享同一 bank，
FLEXA 环的解码写 + GPU 读每帧都和 GPU/DPU 流量抢 SRAM3/4 带宽；SRAM5/6 是 M55 AXI 上的独立
bank，H26D 一跳直达（无 cbus 桥），把解码环流量与 GPU/DPU 流量分到不同 bank。

**结论：FLEXA 环放 SRAM5/6 有效，单帧省 ~2ms、端到端 +2fps(~6.6%)，`DL_FLEXA_RING_ON_SRAM=1` 保留为默认。**

### 12.4 A/B 验证：FLEXA 环深度 seg=3 vs seg=4

隔离"环深度"变量：FLEXA 环强制放 HSRAM（`DL_FLEXA_RING_ON_SRAM=0`，HSRAM 有 443KB
空间，seg=3/4 都放得下），只改 `DL_SEG_NUM_SHALLOW`（3↔4），其它条件不变。

| 指标 | seg=3 (HSRAM) | seg=4 (HSRAM) | 差值 |
|---|---|---|---|
| I 帧 avg | 29.7 ms | 29.19 ms | −0.5 ms |
| P 帧 avg | 32.68 ms | 32.51 ms | −0.17 ms |
| 混合 fps | ~30.85 fps | ~31.0 fps | +0.15 fps |

差异 ≤0.5ms / ~0.15fps，**完全落在测量噪声内**（单个 5s 窗 I 帧仅 5~6 个样本）。
说明浅环 3 段已足够喂饱 GPU 流水，加深到 4 段并不改善解码/GPU 的重叠——瓶颈在 GPU
rotate+compress 的**计算**，不在环深度。旁证：seg=4 时 `flexa sync from early block`
由 `line 2` 变 `line 3`（环更深、GPU 追早一个 block 边界），但整体吞吐不变。

**结论：seg 4→3 对帧率无影响，是"零代价"减容——省 ~30KB HSRAM 换来 FLEXA 环塞进
SRAM5/6 的空间。最优组合 = `DL_SEG_NUM_SHALLOW=3` + `DL_FLEXA_RING_ON_SRAM=1`。**

### 12.5 方案 A：GPU `scale=false`（无收益，已验证）

`doorbell_display_compositor.c` 的 `gpu_cfg.scale` true→false。源码分析先行结论：
`bk_gpu_ctlr_default.c` 中 `scale` 仅在 `config->scale` 为真时 concat
`vg_lite_scale(scale_x, scale_y)`，而 `scale_x=scale_y=output/input`；本工程
input=output=1280×720 → 缩放因子恒为 1.0（恒等），且 flexa blit 固定用
`VG_LITE_FILTER_POINT`（最近邻，无 filter 开销）。故 `scale=false` 与 `scale=true`
得到的变换矩阵完全相同。

| 指标 | scale=true (基准) | scale=false | 差值 |
|---|---|---|---|
| I 帧 avg | 27.9 ms | 28.0 ms | +0.1 ms |
| P 帧 avg | 30.6 ms | 30.8 ms | +0.2 ms |
| 混合 fps | ~32.9 fps | ~32.7 fps | −0.2 fps |

实测差异全在噪声内，**方案 A 零收益，与源码预测一致**。`scale=false` 作为"删冗余参数"
的清理保留（无害），不作为性能手段。

### 12.6 下行帧率优化天花板结论

- GPU 已满频 480M（`gpu_core.c` 强制 `PM_CPU_FRQ_480M`+`CKSEL_GPU_480M`），无提频空间。
- I 帧地板 ~27.9ms(≈35.8fps) ≈ 纯 GPU rotate+compress 吞吐；P 帧多 ~2.9ms 全在解码侧
  PSRAM0 参考帧读。
- **面板刷新 36Hz(rps 36) 是绝对硬顶**，`lcd_wait≈8µs` 说明 DPU 不是瓶颈。当前 ~32.9fps
  离硬顶仅 ~3fps。
- `scale=false`（方案 A）已证无效；`compress=false`（方案 B）预计适得其反：GPU 改写
  未压缩 ARGB 会**增加** PSRAM 写字节数（压缩本是省带宽），且输出缓冲 4× 膨胀
  （3×3.7MB=11MB + 输入 slot 2.76MB > CODED 12.9MB，放不下），故不推荐。
- **实际下行源即 30fps，管线上限 32.9fps 已达标**；真实通话下行掉帧的根因是网络收包
  （日志反复出现 `requestKeyFrame{"reason":"frameLoss"}`），不在解码显示链。
- **建议：停止管线微优化；若仍需提升真实下行帧率，转向 WiFi 收包/抗丢帧侧。**

### 12.7 方案 C：解码器 DPB 可覆盖写（无效 + 破坏正确性，已回退）

试点：`private_h264_decode_ctlr.h` 的 `h264_decode_mem_malloc` 对 ≥512KB 的 PSRAM0
帧挂 `BK_FRAME_BUFFER_FLAG_WRITE_THROUGH`（cover-write），想减小 recon 写的 RMW、
给参考读腾带宽。

| 指标 | 基准 | 方案 C |
|---|---|---|
| I 帧 avg | 27.9 ms | ~30.6 ms* |
| P 帧 avg | 30.6 ms | 30.6 ms（无变化）|
| 混合 fps | ~32.9 fps | ~32.8 fps |

*该轮 I 帧 AU 较大，I/P 几乎同值；关键是 **P 帧 avg 一点没降**。

两个结论：
1. **无效**：P 帧成本不变 → P 帧瓶颈不是写带宽，而是 **motion compensation 对参考帧的
   散乱读延迟**；cover-write 只优化写，帮不了读。
2. **破坏正确性**：运行一会画面出现**彩色马赛克**。解码器帧是"写一次 recon、之后作为
   参考被反复回读"，对这种又写又读的缓冲开 cover-write 会导致回读到不一致数据，参考链
   损坏→P 帧雪崩。**cover-write 只适合只写不回读的缓冲（如纯 GPU 输出帧），绝不能用于
   DPB 参考帧。**

**结论：方案 C 已回退。至此 PSRAM 拆分 / 可覆盖写两条路对纯解码显示帧率均无进一步收益，
解码显示链维持 §12 基准（~32.9fps）为最终优化结果。**

### 12.8 杠杆 1：系统堆移 HSRAM + GPU ping-pong 移 SRAM5/6（+~1fps，已验证保留）

思路：AP 访问 SRAM5/6 比 HSRAM(SRAM3/4) 快（§12.3 已证，FLEXA ring 移 SRAM +2fps）。
GPU 的 flexa 输出 ping-pong（rotate+compress 的渲染目标，~41KB）此前落在 HSRAM，想
也搬进 SRAM5/6。但 SRAM5/6 起始只够放 ring(~90KB) **或** ping-pong 之一。故引入"杠杆 1"：
把通用 `os_malloc` 默认堆从 SRAM5/6 改到 HSRAM（`CONFIG_OS_HEAP_USE_HSRAM=y`），腾出
SRAM5/6 让 **ring + ping-pong 同时进 SRAM**。

改动：
- `defconfig`：`CONFIG_OS_HEAP_USE_HSRAM=y`（系统 os_malloc → HSRAM）。
- `doorbell_downlink_video.c`：`DL_FLEXA_RING_ON_SRAM=1`（ring 保持 SRAM）。
- `bk_mem_sram.c`：`GPU_FLEXA_OUTPUT_ON_SRAM=1`，`bk_get_gpu_output_buffer` 优先
  `os_sram_malloc`（带 24KB 安全余量），不足回退 `hsram_malloc`。

落点验证（开机日志）：
```
DL FLEXA ring -> SRAM5/6 pp=92160 sram_free 134048->41784 hsram_free=461216
gpu flexa pingpong -> SRAM5/6 base=0x2c1b44c8 size=41024
```
ring + ping-pong 均进 SRAM5/6（装完 ring 剩 ~41KB，ping-pong 41024 刚好塞入，未回退）。

| 指标 | 基准(§12) | 杠杆1后 | 变化 |
|---|---|---|---|
| P 帧 avg | 30.6 ms | ~29.8 ms | −0.8 ms |
| 混合 fps | ~32.8–32.9 fps | ~33.7–33.9 fps（多窗口稳定复现） | **+~1fps（~3%）** |

反证：P 帧 30.6ms→29.8ms（1/0.0306=32.7 → 1/0.0298=33.6fps），与 fps 维度 +~0.9fps
完全对上 → 排除"仅 fps 统计噪声"，是 ping-pong 进 SRAM 真实省下的访存时间。

结论：**幅度小但真实、可稳定复现的 +~1fps；改动小、无功能风险，保留为默认配置。** 幅度
有限的原因同 §12.6——P 帧主瓶颈是运动补偿对参考帧的散乱读延迟，ping-pong 只优化 GPU
输出侧的写/读，能省有限（ring 进 SRAM 拿 +2fps，ping-pong 再拿 +1fps，边际递减符合预期）。

> 注：本轮用 GOP 模拟测试宏（1 I + 29 P 循环）测性能，"运行一会出现累积彩色马赛克"是该
> 测试宏的固有现象（缓存的 P 帧参考链与缓存 I 不匹配 + 同一 P 反复叠加导致 drift 累积），
> **与本改动无关，不代表真实通话画质**；判性能只看 `cost(GOP) I/P` 与 fps。

### 12.9 方案 D：example 式 PSRAM 可覆盖写窗口（与杠杆 1 持平，无额外收益）

对照 `h264d_gpu_display_example`：对整池开 **一条** write-through 窗口（64B 对齐）。
室内屏堆与 example **功能对应、颗粒对调**。

**上一份 12.9 作废。** 那次接听后仍开了本机上行（`intercom media start: uplink +`、
`h264e_debug_callback` ~2.1Mbps），P 帧 31.3ms / 混合 ~32.0–32.3fps 是 **上行抢带宽**，
不能用来评价 write-through。本轮关 `CONFIG_DOOR_ACCESS_CALL_LOCAL_UPLINK` 后重测。

有效条件（`ReceivedTofile-COM22-2026_9_15_17-05-39.DAT`）：

```text
intercom media start: (no uplink) downlink 1280x720 up
H264 decoder opened (recon write-through compiled in)
out pool cover: write-through 0x64760bc0-0x64a03cc0 slots=3 area=4
DL TEST: cached I-frame AU=33240 bytes
DL TEST: cached P-frame AU=38 bytes
DL TEST: GOP ARMED (1 I + 29 P looping)
```

全程无 `h264e_debug_callback`；MONITOR 只剩 GPU/DPU（无 ISP MP），与 §12 纯下行一致。

| 窗口 | 室内屏落点 | 本轮 |
|---|---|---|
| GPU 压缩 ARGB 输出池 3 slot | PSRAM1 / `MEM_SLAB_HEAP_CODED` | **已生效** `area=4`，`0x64760bc0–0x64a03cc0` |
| 解码 recon/DPB + MV | PSRAM0 / `MEM_SLAB_HEAP_UNCODED` | **编译进固件**（open 日志已确认）；本 log **仍无** `dec cover: recon write-through` / `skip` / `failed` |

解码窗口在首帧分配 recon 时才会 enable。I 缓存（29633）到 GOP ARMED（29918）之间没有
对应日志，因此 **不能认定 DPB 运行时窗口已打开**（可能 UART 丢掉 `vcdec_h264_dpb` tag，
或 `cover_enable` 未走到）。评价本轮帧率时，可确认的只有 **GPU 池 WT + 无上行**。

舍弃起步窗（`skip 12`、ivl max 227ms、~31.9fps）。其后 10 个 5s 窗均为 `wifi=0 skip=0`：

```text
~33.8fps  I avg 26.5ms  P avg 29.83ms  gpu_out ~29.71ms  lcd_wait ~6µs
~33.8fps  I avg 26.8ms  P avg 29.83ms  gpu_out ~29.73ms
~33.8fps  I avg 26.6ms  P avg 29.85ms  gpu_out ~29.76ms
~33.8fps  I avg 26.8ms  P avg 29.81ms  gpu_out ~29.71ms
~33.8fps  I avg 26.7ms  P avg 29.80ms  gpu_out ~29.72ms
~33.8fps  I avg 26.8ms  P avg 29.82ms  gpu_out ~29.72ms
~33.8fps  I avg 26.7ms  P avg 29.86ms  gpu_out ~29.75ms
~33.8fps  I avg 26.9ms  P avg 29.79ms  gpu_out ~29.70ms
~33.7fps  I avg 26.7ms  P avg 29.87ms  gpu_out ~29.76ms
~33.8fps  I avg 26.4ms  P avg 29.86ms  gpu_out ~29.76ms
```

| 指标 | §12 基准 | 杠杆 1（§12.8） | **作废的上行+WT log** | **本轮（无上行 + GPU 池 WT）** |
|---|---|---|---|---|
| I 帧 avg | 27.9 ms | ~27.9 ms | 26.1–28.4 ms | **~26.7 ms** |
| P 帧 avg | 30.6 ms | **~29.8 ms** | 31.3–31.5 ms | **~29.83 ms** |
| 混合 `h264d+gpu_wait` fps | ~32.9 fps | **~33.7–33.9 fps** | ~32.0–32.3 fps | **~33.7–33.8 fps** |
| `gpu_out` ivl avg | 30.5 ms | ~与解码一致 | 32.0–33.6 ms（max ~59ms） | **~29.7 ms**（与解码对齐，max ~32ms） |
| `lcd_wait` | 0/9/32 µs | 同左 | 0/6–8/32 µs | 0/5–9/32 µs（偶发 531µs） |
| MONITOR GPU/DPU | 32–33 | 33–34 | 30–32（含上行） | **33–34 / 33–34**，rps 36 |

**结论：关上行后，方案 D 与杠杆 1 极限帧率持平，没有再提升。** P 帧仍钉在 ~29.8ms
（1/0.02983 ≈ 33.5fps），混合 33.8fps 与 §12.8 相同，差在统计噪声内。I 略快来自本轮
P AU 仅 38B、画面更静，不是 write-through 把 I 解出来了。

上一轮“WT 降 1.5fps”应改为：**那是上行 ISP+H.264 编码抢 PSRAM/CPU，不是 GPU 池窗口的代价。**

方案 D 仍不作为性能默认：GPU 池窗口已打开但 **P 帧地板没动**（瓶颈仍是参考帧读，§12.7）；
解码 recon 运行时窗口本 log 未证实。极限帧率仍以 **§12.8 杠杆 1 的 ~33.7–33.9 fps** 为准。

### 12.10 方案 E：PSRAM 分芯片 + 解码 recon WT（应用侧），~36fps 封顶，GPU 池 WT 因 assert 关闭

在方案 D 基础上做了两件事，并解决了随之而来的死机：

1. **分芯片布局落地 + 解码 recon 可覆盖写移到应用侧**：GPU 输出池放 CODED/PSRAM1，
   解码 recon/DPB/MV 放 UNCODED/PSRAM0。不改内部库 `libvcdec`，改为在开放的解码器
   分配器 `h264_decode_mem_malloc`（`bk_vpu/bk_decoder/h264d/src/h264_decode_mem.c`）
   上挂 cover，只覆盖**物理相邻的一串**解码块（≥64KB、间隙 ≤256B 才合并），日志
   `dec cover: recon write-through ... area=0 psram0`，运行时**已证实生效**（补上了
   §12.9 缺的那一环）。
2. **修复 HAL 跨芯片清零 bug**：`psram_hal.c` 的 `disable_write_through_area` 原来会把
   PSRAM0/PSRAM1 同一 hal_id 一起清零，导致 area0（解码，PSRAM0）每次重编都误关
   area4（GPU 池，PSRAM1）。改为按 area 归属只清对应芯片（interleave 仍两颗都清）。

**帧率跳到 ~36fps 并被 36Hz 屏封顶**（`ReceivedTofile-COM22-2026_9_15_17-47-46.DAT`、
`_19-14-14.DAT`）：`h264d[ok 180f]/5s` = 36fps，`cost(GOP)` P/I ≈ 27.76ms，
`lcd_wait` 从 §12.9 的 ~6µs 升到 ~4.8ms、rps 钉在 36——**瓶颈从解码/GPU 转到显示端**。

**但连续两次十几秒内网络任务 MPU fault**（`tcp/ip`、`ntwk_tcp_vid_cli`，栈均在 PSRAM1
`0x64d0x~0x64d2x`，保存现场被斜坡毒值覆盖、`r4=0` → `ldr [r4]` DACCVIOL）。
根因：**任务栈在 PSRAM1（`CONFIG_TASK_STACK_IN_PSRAM`），GPU 池 WT cover 也在 PSRAM1，
同颗芯片 write-through 干扰了 CPU 对任务栈的小粒度写**。cover 寄存器实测并未在地址上
压到栈，属控制器写路径层面的干扰，对应驱动 API “single master only” 约束。
详见 `docs/psram_write_through_assert.md`。

**修复：只关 PSRAM1 上的 GPU 池 cover**（`comp_out_pool_init` 不再调用
`comp_out_pool_cover_enable`，池仍留在 CODED/PSRAM1），保留 PSRAM0 解码 cover
（该芯片无任务栈）。

复测（`ReceivedTofile-COM22-2026_9_15_19-43-22.DAT`，挂测 ~125s）：

```text
intercom media start: (no uplink) ...
dec cover: recon write-through 0x60ce9c80-0x60fc5180 cnt=3 area=0 psram0   ← 保留
（无 out pool cover: 行）                                                    ← 已关
全程无 @PRIMARY_EXCEPTION / Dump-reason
```

| 指标 | §12.8 杠杆1 | §12.10 分芯片（GPU cover 关） |
|---|---|---|
| h264d | ~33.7–33.9 fps | **36fps**（`ok 180f fail 0 skip 0`/5s） |
| P 帧 avg | ~29.8 ms | **~27.76 ms** |
| I 帧 avg | ~27.9 ms | ~27.76 ms |
| `gpu_out` ivl avg | ~29.7 ms | ~27.77 ms |
| `lcd_wait` | 0/6/32 µs | **~4.8 ms**（显示端成为瓶颈，rps 36 封顶） |
| 稳定性 | 稳定 | 挂测 ~125s 无 assert |

**结论：**
- **36fps 来自 PSRAM 分芯片 + SRAM 数据面，不依赖任何 write-through cover**——关掉
  GPU 池 cover 后帧率不变，直接证明 cover 在被 36Hz 封顶的场景下无额外帧率贡献。
- write-through cover 与 PSRAM 任务栈同芯片时不安全；GPU 池 cover 已永久关闭，
  解码 recon cover 保留（PSRAM0 无栈，未见死机）但同样对 36fps 无贡献，追求最大
  稳健可后续一并关掉。
- 极限帧率更新为 **~36fps（36Hz 面板封顶）**；进一步提升需提高面板刷新率或换 rps。

### 12.2 结论 / 优化方向

- 当前管线地板 ~30.5ms（~32.9fps），已超过 24/30fps 源，端到端瓶颈在网络侧。
- 若要继续压 P 帧成本，重点在 **PSRAM0 参考帧读带宽**（解码器 recon 写 + 参考读仍同压 PSRAM0）。
- 若要压 I 帧成本（已接近 36Hz 面板上限），重点在 GPU rotate+compress 计算，边际收益小。
- **复测口径**：取 `skip=0` 稳态窗，记录上表 6 个指标，与本基准逐项对比。

---

## 13. SDK 同步后的帧率回归（32.7→36→44fps）与极限帧率实测

### 13.1 背景

一次 `repo sync` 后，下行帧率从 §12.10 的 36fps 掉到 **32.7fps**。本节记录
定位过程、被证伪的假设、最终根因，以及解除面板封顶后实测到的管线真实上限。

### 13.2 逐步恢复过程

| 阶段 | fps | 动作 |
|---|---|---|
| sync 后基线 | 32.7 | — |
| + FLEXA 环入 SRAM5/6 + `seg=3` + 杠杆1 | 33.4 | 解码环与 GPU/DPU 分 bank（§12.3/§12.8 回移） |
| + GPU ping-pong 入 SRAM + 关 USB | 34.2 | 关 USB 腾出 SRAM5/6 容纳 ping-pong |
| **+ GPU 输出池修回 PSRAM1** | **36.2**（36Hz 面板封顶） | **修复 sync 破坏的 PSRAM 分芯片** |
| **+ 面板 36Hz → 50Hz** | **44.2**（管线真实上限） | 解除面板 pacing |

### 13.3 根因：GPU 输出池被 sync 从 PSRAM1 改到 PSRAM0

`doorbell_display_compositor.c` 中主输出池的两处分配被同步改写：

```c
// v2（36fps）
ptr = bk_frame_buffer_malloc(COMP_OUT_HEAP, size);          // MEM_SLAB_HEAP_CODED = PSRAM1
// sync 后（34fps）
ptr = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, size);  // PSRAM0
```

`#define COMP_OUT_HEAP MEM_SLAB_HEAP_CODED` 以及 "On PSRAM1 for the chip-split
bandwidth win" 的注释**都还在，只是没人引用了**，所以 code review 和"查宏定义"
都发现不了（排查中据此误判过一次）。

后果：PSRAM 分芯片失效，**解码参考帧读 + GPU 输出写 + DPU 刷屏读全挤在 PSRAM0**。

**为什么只影响 P 帧**：I 帧不读参考帧，PSRAM0 压力小，耗时不变（23.5ms）；
P 帧要多读 ~1.38MB 参考帧，与 GPU 写 / DPU 读抢同一颗 PSRAM0 → **+6.6ms**。
实测 I/P 不对称正是这个机理的指纹。

修复后 P−I 差从 6.6ms 降到 **3.4ms**（分芯片消掉了大部分争用，剩下的是帧间
解码读参考帧的固有成本）。

### 13.4 被证伪的假设（避免后人重复踩坑）

排查中提出并**逐一实测否定**的假设：

| 假设 | 判别方法 | 结果 |
|---|---|---|
| recon cover 形态（合并 vs 逐块） | 回移 v2 应用侧合并 cover | 无差别（且导致 boot 动画花屏，已回退） |
| recon DIR_MV 布局（commit `7dc3391`） | 把 `recon_alloc_size` 改回 `copy_size` | 无差别 |
| VG 命令缓冲 8KB→32KB | `CONFIG_VG_LITE_GPU_COMMAND_BUFFER_SIZE=0x8000` | 无差别（略降，多耗 49KB HSRAM，已回退） |
| 任务栈是否过 L2 Cache | `ram_regions.csv` 去掉 `AP_PSRAM_CACHE_HEAP` | 无差别 |
| AP 空闲 WFI（`arch_sleep`） | 查生成 config | `CONFIG_PM_AP_WFI_ENABLE` 未设置，未编入 |
| vcdec / vg_lite 代码回归 | v2↔当前源码全量 diff | 解码热路径**逐字节相同**，无预编译库 |

**教训**：recon cover 本身是有效的（判别实验：关掉后 33.4→26.6fps，每帧多 8ms），
但它**不是** 34↔36 的差异来源。有效 ≠ 是本次回归的原因。

### 13.5 决定性方法：SDK 交换二分

代码考古连续失败后，用**交换 SDK/solution** 把问题一刀切成两半：

```bash
# v2 solution + 当前 SDK
cd old/.../bk_solution_doorbell_release_4.0.1/projects/intercom_indoor_display
make bk7259 SDK_DIR=<当前 SDK> -j32
```

结果 **36.2fps** → 回归在**方案侧**，SDK 无责。搜索空间从整个 SDK 缩到 solution，
再 diff 两边 `smart_intercom` 源码即命中。

（反向"当前 solution + v2 SDK"编不过：当前 solution 依赖 SDK 新增的
`cs2_congestion_drop.h`。v2 solution 配当前 SDK 只缺 `VG_LITE_GPU_CONTIGUOUS_MEM_SZ`
这一个 Kconfig，补回定义即可编过。）

### 13.6 防回归

`comp_out_pool_init()` 增加启动 log，按池首地址判断实际落在哪颗 PSRAM：

```
正常  db-comp:I: out pool: 3 x 921600 @0x64922cc0 -> PSRAM1 (chip-split OK)
异常  db-comp:E: out pool: 3 x 921600 @0x60xxxxxx -> PSRAM0, EXPECTED PSRAM1!
                 chip split broken, downlink loses ~2ms/P-frame
```

异常走 **E 级别**，不会淹没在 I 级日志里。任何一次 sync 后看这一行即可。

### 13.7 极限帧率实测（面板 50Hz，解除 pacing）

面板 `er68576b` 由 36Hz 提到 50Hz 后，管线不再被封顶，真实能力显现：

| 指标 | 36Hz 面板（被 pacing） | **50Hz 面板（自由运行）** |
|---|---|---|
| I 帧 | 27.76 ms（假象） | **19.4 ms ≈ 51.6 fps** |
| P 帧 | 27.76 ms（假象） | **22.76 ms ≈ 43.9 fps** |
| 混合(1I+29P) | 27.76 ms | **22.64 ms → 44.2 fps** |
| 实际出帧 | 36 fps（面板上限） | **44.2 fps（管线上限）** |
| MONITOR | `DPU[fps 36 rps 36]` | `GPU[44f] DPU[fps 44 rps 50]` |

在 36Hz 下 I/P 都显示 27.76ms，那是**被面板 pacing 拉平的假象**；解除封顶后
才看到 I 19.4ms / P 22.8ms 的真实分布。连续十余个 5s 窗稳定 44.2–44.3fps，
`ok 220f fail 0 skip 0`，无掉帧无 assert。

**面板 DSI 链路预算**：`900×1316×50×24×1.3/4 = 462 Mbps/lane`，上限
550 Mbps/lane，余量 16%。（36Hz 时为 332 Mbps/lane。）

**结论：下行解码显示管线极限 ~44fps**，瓶颈在 P 帧解码（22.76ms，其中比 I 帧
多出的 3.4ms 是 PSRAM0 参考帧读 + motion compensation）。实际下行源 30fps，
余量 47%。

### 13.8 产线最终配置

**方案侧**
- `doorbell_display_compositor.c`：主输出池 `COMP_OUT_HEAP`（PSRAM1）+ 分芯片校验 log；PIP 池保持 UNCODED。
- `doorbell_downlink_video.c`：`DL_SEG_NUM_SHALLOW=3`、`DL_FLEXA_RING_ON_SRAM=1`、
  `DL_TEST_LOOP_GOP=0`（harness 保留在树里，改 1 即可复测上限）、`DL_TEST_DISABLE_RECON_COVER=0`。
- `defconfig`：`CONFIG_OS_HEAP_USE_HSRAM=y`（杠杆1）、`# CONFIG_USB is not set`（腾 SRAM）、
  `CONFIG_PSRAM_WRITE_THROUGH=y`。
- `ram_regions.csv`：保留 `AP_PSRAM_CACHE_HEAP`（任务栈过 L2 cache）。

**SDK 侧改动（`repo sync` 会覆盖，需记 patch）**
| 文件 | 改动 | 说明 |
|---|---|---|
| `bk_frame_buffer/src/bk_mem_sram.c` | `bk_get_gpu_output_buffer` SRAM5/6 优先、HSRAM 兜底 | GPU ping-pong 分 bank |
| `bk_gpu/src/bk_gpu_ctlr_default.c` | ping-pong 释放 `hsram_free` → `os_free` | **必须配套**，否则 SRAM 指针还进 HSRAM 堆会损坏 |

**面板刷新率：产线保持 36Hz。** §13.7 的 50Hz 只用于暴露管线上限，已回退。
理由：协商的下行源是 30fps，36Hz 面板对 30fps 源与 50Hz 没有任何可见差异；
50Hz 还会多耗 DSI 带宽/功耗，且超出面板额定刷新需查 datasheet 确认。
要真正吃到 44fps 能力，必须**同时**提高 `doorbell_rpc_solution.c` 里协商的下行
fps（当前 30）并提高面板刷新率，二者缺一无效。

**面板驱动裁剪（本工程 defconfig）**：只保留链接期真正引用的两款——
`ER68576B`（本产品面板，`ap_main.c` 引用）与 `HX8399C`（共享组件
`devices_cli.c` 引用）。面板驱动全是 `const` 数据（命令表 + `bk_display_dsi_panel_t`），
位于 **FLASH rodata，不占 SRAM**，裁剪只省 flash。

**PSRAM 归属（务必保持）**

| 用途 | 芯片 |
|---|---|
| 解码 recon/DPB（参考帧读写） | PSRAM0（UNCODED, 0x60000000） |
| GPU 输出池 / DPU 刷屏源 | **PSRAM1**（CODED, 0x64000000） |
| PIP 自视图池 | PSRAM0（量小，不影响） |

