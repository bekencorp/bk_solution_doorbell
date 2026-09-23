# PSRAM 可覆盖写导致网络任务 MPU Fault 的 RCA 报告

## 1. 结论摘要（以 §12.16 / §12.17 为准；早期段落的机制表述已被后续实验更正）

室内机下行链路开启 PSRAM write-through（可覆盖写）cover 后，多次在 **网络任务**
（`tcp/ip` / `wdrv` / 下行视频 TCP 客户端）上死机（`MemFault` / `UsageFault·INVSTATE`
/ `wdrv tx_alloc_num==0`），共同特征是被抢占任务保存在 PSRAM1 栈上的上下文被破坏。

**根因（收敛后，见 §12.17）**：BK7259 的 RTOS 任务栈默认落在 **`AP_PSRAM_HEAP`，
其 MPU 属性是 attr 1 = non-cacheable**（`mpu_cfg.c` 默认策略）。旧工程 `ram_regions.csv`
里没有 `AP_PSRAM_CACHE_HEAP`，任务栈分配路径 `task_stack_malloc → psram_cache_malloc`
因 `CONFIG_AP_PSRAM_CACHE_HEAP_ADDR` 未定义而回退成 `psram_malloc`，即栈落在
**非 cache 的 PSRAM1**。于是每次上下文保存/恢复都是直连 PSRAM 控制器的小粒度非 cache
读写；当**同一颗 PSRAM1 上有活动的 write-through cover 整行刷写流量**时，两者在控制器
写路径上相互破坏，改写被抢占任务的 callee-saved 现场（`r4=0`、xPSR 丢 T 位等），恢复后
`ldr r3,[r4]` 或 `POP {PC}` 触发 fault。

与 cover 的写 master 无关（GPU/HPDMA 或解码器均可复现，§12.16）；也不是地址重叠
（窗口与栈不重叠）。触发的必要组合是 **“PSRAM1 上活动 cover” ⊕ “非 cache 的任务栈在
PSRAM1” ⊕ 真实并发流量**。

**修复方向**（任一即可打断上面的组合）：
1. 让任务栈**过 cache**：`ram_regions.csv` 增加 `AP_PSRAM_CACHE_HEAP`（attr 5，L2
   write-back），任务栈自动改从该 cache 堆分配（对齐 `video_intercom`，§12.17 待复测）；
2. 任务栈/队列放 **SRAM**（§12.13.1 已验证 9 分钟稳定）；
3. 产线**不开 cover**（36fps 不依赖，最省事）。

> 历史说明：§1 早期版本把根因写成“GPU 当 master、非 32B 整行写违反 API”“cover 与栈
> 同颗 PSRAM 即冲突”，均已被 §12.8/§12.11.2/§12.14/§12.16 逐条更正——真正的变量是
> **任务栈是否 non-cacheable 且与活动 cover 同颗芯片**，而非哪个 master 写 cover。

---

## 2. 输入与构建确认（ELF ↔ banner gate）

- 日志：`ReceivedTofile-COM22-2026_9_15_19-14-14.DAT`（本次）与
  `ReceivedTofile-COM22-2026_9_15_17-47-46.DAT`（上一次，同根因）。
- Dump banner：`build time => Sep 15 2026 19:08:38`。
- ELF：`build/bk7259/intercom_indoor_display/bk7259_ap/app.elf`，`addr2line` 对
  `pc=0x04209606` 干净解析到 `ntwk_tcp_service.c`，符号与构建匹配（gate 通过）。

---

## 3. Assert / Fault 身份与寄存器现场（本次 19:14）

```
@PRIMARY_EXCEPTION reason=0x12 core=3 pc=0x04209606 lr=0x00000000 sp=0x64d2a3d0 CFSR=0x00000082
@Dump-reason: MemFault      SMP-Core-id:1
r4=0x0  r5=0x0  r9=0x09090909  r10=0x10101010  r11=0x11111111  r6=0x474(=rcv_len)
CFSR=0x82  → DACCVIOL(bit1) + MMARVALID(bit7)；init:D 亦打印 "reason - mpu fault"
```

- `pc=0x04209606` →
  `ntwk_tcp_video_client_receive_data` (inlined in `ntwk_tcp_video_client_thread`)
  `ntwk_tcp_service.c:1321/1441`。
- 反汇编：`pc` 处为 `recv()` 返回后的 `ldr r3, [r4, #0]`（重新加载全局
  `video_tcp_client_service`）。`r4` 应为该全局变量地址，实测 `r4=0` → 读 0 地址
  → MPU DACCVIOL。
- `r4` 是 callee-saved 寄存器，在收包循环里跨 `recv()` 调用保持不变，只能在**任务
  被抢占-恢复**时从其 PSRAM1 栈现场恢复。`r9/r10/r11 = 0x09090909/0x10101010/
  0x11111111` 的连续斜坡毒值，说明恢复出来的整段现场已被覆盖。

---

## 4. 两次死机对照（同一根因）

| 项 | 17:47 | 19:14 |
| --- | --- | --- |
| Dump-reason | MemFault / MPU | MemFault / MPU（CFSR=0x82） |
| 出错任务 | `tcp/ip`，栈 `0x64d0a980-0x64d0b180`（PSRAM1） | `ntwk_tcp_vid_cli`，栈 `0x64d297f8-0x64d2a3f8`（PSRAM1） |
| 现场 | 网络任务寄存器被写坏 | `r4=0`，`r9/r10/r11` 斜坡毒值 |
| PSRAM1 write-through | GPU 池 cover 生效 | GPU 池 cover 生效 |

两次都是 **PSRAM1 上的网络任务栈**被破坏，且都在 **PSRAM1 有 write-through
cover** 的前提下发生。

---

## 5. cover 寄存器实测（本次 dump 解码）

dump `region: PSRAM0/PSRAM1`（`0x48060000 / 0x48070000`）中 cover 寄存器
（word 0x10..0x17，20-bit 偏移，32B 单位）：

| 芯片 | area | START | STOP(EN) | 覆盖 | 判定 |
| --- | --- | --- | --- | --- | --- |
| PSRAM0 | 0 | `0x60c08c00` | `0x60ee4100` | 解码 recon/DPB/MV | 正确、连续、解码独占 |
| PSRAM1 | 0 | `0x64760bc0` | `0x64a03cc0` | GPU 输出池（3×压缩 ARGB） | 正确、连续 |
| 其余 area | — | 0 | 0（未使能） | — | — |

要点：
- HAL 跨芯片清零 bug 已修复，两窗口各在本芯片，未再互相清零。
- 两窗口都**没有**在地址上覆盖 `0x64d29` 任务栈 → 排除“地址直接重叠”，指向
  控制器写路径干扰。

---

## 6. 机理分析（生命周期 / 硬件约束）

- 任务栈：`CONFIG_TASK_STACK_IN_PSRAM=y`，`PSRAM_HEAP @ 0x64d08000`（PSRAM1）。
  网络任务（`tcp/ip`、`ntwk_tcp_vid_cli`、`ntwk_tcp_aud_cli`）栈均在
  `0x64d0x~0x64d2x`（PSRAM1）。CPU 对栈做大量小粒度写。
- GPU 输出池：`COMP_OUT_HEAP = MEM_SLAB_HEAP_CODED`（PSRAM1），并在其上开
  write-through cover。
- 硬件约束：write-through cover 是整颗芯片写路径的模式（单 master、32B 整行
  刷写、无 read-modify-write）。同一颗 PSRAM1 上，cover（GPU 写）与 CPU 对任务栈
  的非整行写共存时，会丢失/串扰 CPU 的部分写 → 破坏被抢占任务的保存现场。
- 这解释了为何两次都崩在 PSRAM1 网络栈、现场被斜坡毒值覆盖、且与 cover 地址不重叠。

---

## 7. 假设判定矩阵

- `H_ASSERT_SITE`：CONFIRMED — `pc` = `recv()` 后 `ldr r3,[r4]`，`r4=0`。
- `H_CONTEXT_CORRUPT`：CONFIRMED — callee-saved 现场（r4-r11）被斜坡毒值覆盖。
- `H_COVER_ADDR_OVERLAP`（cover 直接压栈）：REJECTED — 寄存器实测两窗口不覆盖栈。
- `H_PSRAM1_WT_VS_STACK`（同芯片 write-through 干扰 CPU 栈写）：CONFIRMED（高） —
  两次死机均满足“PSRAM1 有 cover + PSRAM1 栈被破坏”，且非地址重叠。
- `H_HAL_CROSS_CLEAR`（上一轮的跨芯片清零）：已在上一次修复，本 dump 已不复现。
- `H_FPS_NEED_COVER`：REJECTED — 实测 `~36.2fps` 已被 36Hz 屏封顶
  （`lcd_wait` 升高），cover 无额外帧率贡献。

---

## 8. 一句话结论

任务栈在 PSRAM1，GPU 输出池 cover 也在 PSRAM1，同芯片 write-through 干扰了 CPU
对网络任务栈的小粒度写，破坏保存现场导致 MPU fault；关闭 PSRAM1 上的 GPU 池
cover 即可根除。

---

## 9. 修复

文件 `components/smart_intercom/src/doorbell_display_compositor.c`：

- `comp_out_pool_init()` 中**不再调用** `comp_out_pool_cover_enable()`；GPU 输出池
  仍分配在 CODED/PSRAM1（保留分芯片带宽收益）。
- `comp_out_pool_cover_enable()` 保留为参考实现（标记 `__attribute__((unused))`）。
- 保留 PSRAM0 上的解码 recon cover（`CONFIG_H264D_RECON_WRITE_THROUGH=y`），该芯片
  无任务栈，未观察到相关死机。

未改动：SRAM 数据面（Lever 1）、PSRAM0/PSRAM1 分芯片布局、HAL 按芯片清零修复。

---

## 10. 下一步与不确定性

- 复测确认：不再出现网络任务 MPU fault；`out pool cover:` 日志应消失，`dec cover:`
  仍在；核对下行帧率是否维持在 ~36fps（若回退则说明 36 依赖 GPU cover，需再评估）。
- 残留风险（低）：PSRAM0（UNCODED）上仍有 PIP/其它 CPU 写者与解码 cover 共存，
  目前未见死机；若后续 PSRAM0 上出现同类现场，则解码 cover 亦应关闭。
- 产品默认建议：`DL_TEST_LOOP_GOP=0`；write-through 在被 36Hz 封顶的场景下无收益，
  可考虑整体关闭以最大化稳健性。

---

## 11. 复测确认（修复生效）

日志 `ReceivedTofile-COM22-2026_9_15_19-43-22.DAT`，挂测约 125 秒连续下行：

```text
intercom media start: (no uplink) downlink 1280x720 up
dec cover: recon write-through 0x60ce9c80-0x60fc5180 cnt=3 area=0 psram0   ← 解码 cover 保留
（全程无 out pool cover: 行）                                               ← GPU 池 cover 已关
（全程无 @PRIMARY_EXCEPTION / @Dump-reason）                                ← 无死机
```

- **稳定性**：整份日志无任何 exception / assert，上一版“十几秒必崩”的网络任务 MPU
  fault 不再出现。
- **cover 状态**：`out pool cover:` 消失（PSRAM1 不再开窗口），`dec cover:` 仍在
  （PSRAM0 area0，`0x60ce9c80-0x60fc5180`），符合预期。
- **帧率**：稳态 `h264d[ok 180f fail 0 skip 0]`/5s = **36fps**，`cost(GOP)` P/I ≈
  27.76ms，`lcd_wait` ≈ 4.8ms、rps 钉在 36 → 瓶颈在 36Hz 屏，管线已快过屏。
- **判定**：关掉 PSRAM1 的 GPU 池 cover 后帧率不变（仍 36fps），**直接证明 36fps
  来自 PSRAM 分芯片 + SRAM 数据面，不依赖任何 write-through cover**。第 10 节那条
  “是否回退”的不确定性据此消除。

---

## 12. 深入机理：为什么在 PSRAM1 开可覆盖写会 assert

### 12.1 先排除的两点（有实测支撑）

- **不是地址直接重叠**：dump 的 cover 寄存器实测（第 5 节）显示 GPU 窗口
  `0x64760bc0-0x64a03cc0`，任务栈在 `0x64d0x~0x64d2x`，两者相距约 3MB，硬件比较区间
  `[cfg_start, cfg_stop]` 根本不含栈地址。窗口末端 32B 对齐，也不可能延伸 3MB。
- **不是编错寄存器**：仓库里有两份 `psram_hal.c`。`bk7259_ap/hal/psram_hal.c` 是旧实现
  （`psram_ll_set_cover_start(area, start>>5)`，固定单一 reg base、area 4~7 会写到
  `0x18+` 的错误寄存器，无双芯片概念）；`common/hal/psram_hal.c` 是新实现（按
  `reg_base` 区分 PSRAM0/1、`(addr-data_base)>>5`、按 area 归属清零）。dump 里
  **PSRAM1 area0 能读到正确的 cover 值**，只有 `common/hal` 版能产生这一结果 →
  确认实际链接的是 `common/hal` 版，cover 编程本身是正确的。

也就是说：**cover 配置正确、地址不重叠，但同颗芯片上窗口外的任务栈仍被破坏。**
问题因此落在 PSRAM 控制器“开启 write-through 后对整颗芯片写行为的改变”，而非软件把
窗口配错。

### 12.2 write-through（cover）到底改了什么

正常（非 cover）写 PSRAM：控制器对小于一整行（32B）或非对齐的写做
**read-modify-write（RMW）**——先把该 32B 行读进控制器内部 line buffer，改写命中的
字节，再整行写回。CPU 压栈 `push`/`str` 这种 4/8 字节的小写就走这条路。

cover / write-through 写：落在 `[start,end]` 的写被标记为**整行直刷、不做 RMW**
（`bk_psram_enable_write_through` 注释：*The PSRAM hardware will always flush its
internal cache-line, without considering whether the cache line is full or not*，
且 *must only one master access*、*write length must be integer multiples of 32
bytes*）。这套机制是为 GPU/DMA 那种“整行、单 master、对齐”的写设计的。

关键在于：**line buffer / 写 FIFO 是整颗 PSRAM 控制器共享的资源**，不是按 area 或按
地址各自独立。cover 只决定“某笔写是否走直刷”，但直刷写与 RMW 写复用同一套内部行缓冲
与写回状态机。

### 12.3 崩溃机制（推断，置信度：中高）

BK7259 是双 AP 核 SMP，任务栈全在 PSRAM1（`CONFIG_TASK_STACK_IN_PSRAM`）：

- core0：compositor / GPU / DPU，向 PSRAM1 的 GPU 池做**整行 write-through** 写；
- core1：网络 / 解码控制任务，其**任务栈也在 PSRAM1**，运行时对栈做大量**小粒度、
  非对齐的 RMW** 写（压栈、保存现场等）。

当两类写在控制器内部交错时：一笔正在进行 RMW 的 normal 栈写（read 阶段已把某 32B 行
读入 line buffer，尚未写回），被一笔 write-through 整行直刷事务抢占 / 复用了同一 line
buffer 状态；RMW 的写回阶段要么被丢弃、要么把 write-through 刚落的行与自己的旧行混写，
导致 **core1 栈写的部分字节丢失**。

这与现场高度吻合：
- 被破坏的是**被抢占任务保存在 PSRAM1 栈上的 callee-saved 现场**，恢复后 `r4=0`
  （高位字节丢失型损坏），`r9/r10/r11` 残留 `0x09090909/0x10101010/0x11111111`——是
  “部分字节被吞”而非整块乱写；
- 两次死机都发生在 **PSRAM1 上、且有 CPU 栈写并发** 的网络任务；
- 解码 cover 在 **PSRAM0**，该芯片上没有任务栈、没有并发的 CPU 小粒度 RMW 栈写，
  因此没有触发（与“PSRAM0 cover 保留仍稳定”一致）；
- 正好命中 API 的两条硬约束：write-through 区必须 **single master**、写必须 **32B 整数
  倍**——一旦同芯片存在第二个 master（CPU）做非对齐写，前提被破坏。

### 12.4 为什么“关 PSRAM1 的 cover”就好了

关掉 GPU 池 cover 后，PSRAM1 上不再有任何 write-through 直刷写，控制器对整颗 PSRAM1
只跑正常 RMW 路径，core1 的栈写不再被直刷事务干扰 → 现场不再损坏。PSRAM0 只有解码
cover、无 CPU 栈并发，天然满足 single-master，安全。

### 12.5 验证实验（若需进一步坐实机制）

1. **移栈验证**：保持 GPU 池 cover 开，把任务栈堆从 PSRAM1 迁到 SRAM/PSRAM0；若不再
   崩，则证明是“同芯片 CPU 栈 RMW 写 vs cover 直刷”冲突。
2. **单核验证**：保持 cover 开，把所有会访问 PSRAM1 的任务钉到 core0（与 GPU 同核，
   串行化对该芯片的访问）；若崩溃消失/显著降低，佐证是双核并发交错。
3. **定向压测**：用 `psram_test` 在 PSRAM1 开一段 cover，同时另一核对**同芯片、窗口外**
   区域做大量非对齐部分写并校验，直接复现部分字节丢失。

### 12.6 这条“规则”的证据分级（重要：不要当成已证定律）

前文那句“凡与 RTOS 任务栈/CPU 小粒度写共处同颗 PSRAM 就禁止开 cover”，**不是从某份
手册直接抄来的定律**，而是三种强度不同的证据拼出来的工程结论，必须分开看：

1. **一手、硬证据（但只约束 cover 区自身）**——`psram.h` 的 API 注释：
   - `bk_psram_enable_write_through`：*"The write through area must only one master
     access, and the write length must integer multiples 32bytes"*；
   - `bk_psram_free_write_through_channel`：*"used for DMA2D accessing only, do not
     use it for CPU accessing PSRAM"*。
   注意：官方只说**被 cover 的那段区域**要单 master、32B 整行、别让 CPU 访问。**它并
   没有说“同芯片窗口外的其它地址也会受影响”。**

2. **受控 A/B 实验（本问题最直接的因果证据）**：唯一变量是“PSRAM1 上的 GPU 池 cover
   开/关”，其余全不变——
   - 开：17:47、19:14 两次，十几秒内 PSRAM1 网络任务栈现场被破坏、MPU fault；
   - 关：19:43 挂测 ~125s 无任何 assert。
   这确立了**经验因果**：PSRAM1 开这个 cover ⇒ 崩，关 ⇒ 不崩。强度高，但它证明的是
   “相关/因果”，**不等于**证明了具体微结构机制。

3. **机制推断（未证）**：§12.3 的“RMW vs 直刷并发丢写”是能同时解释全部现象的假设，
   但**未经片上寄存器级复现**，属推断。

因此“禁止同芯片共处”这一步，是从 API 明文约束（1）出发、被 A/B 实验（2）支持、用机制
（3）来解释的**外推**，而非一手文档结论。**当前它作为工程防线足够（宁可不开），但不应
被引用为“已证明的硬件定律”。**

### 12.7 决定性实验（用仓库现成 CLI 即可坐实/证伪，无需改代码）

`psram_test` 提供了正好能隔离该假设的两个后台命令（见 `PSRAM_TEST.md`）：

- `psram_test_ext wt_start <start_hex> <size> 1` / `wt_verify_512k`：在 **PSRAM1** 指定
  区间开 write-through 并做写-DMA读-校验；
- `psram_test_ext stack_stress_start`：在 **PSRAM1**（`AP_PSRAM_HEAP`）高频创建/销毁
  任务、持续对任务栈做 CPU 小粒度读写并逐字节校验。

关键实验：**两者并行**，但让 `wt` 的 cover 窗口与 `stack_stress` 的任务栈落在**同颗
PSRAM1、互不重叠的地址**上：

- 若 `stack_stress` 报 `mismatch`（栈数据被破坏）而 `wt` 区自身校验正常 → **直接坐实**
  “同芯片 cover 会干扰窗口外的 CPU 栈写”，机制假设成立；
- 若两者都长时间无错 → 证伪该外推，需另找根因（则应重新审视是否别的因素，如具体
  地址、并发核绑定）。

注意：`PSRAM_TEST.md` 里“`wt_*` 与 `stack_stress` 建议不要同时跑”指的是 **`psram_malloc`
堆空间竞争**，不是硬件写干扰；做本实验时应**手动指定互不重叠的地址**规避堆竞争，不要因
那条提示就放弃实验。

### 12.8 §12.7 实验已执行：外推规则被**证伪**（2026-09-16 14:03 log）

实验固件对 `psram_test.c` 的 `psram_wt_task_main` 做了本地改造：去掉 RAM 镜像 +
DMA 回读 + 比对，只保留“CPU 往 cover 窗口循环写”。理由是我们只需要制造直刷写流量，
不关心窗口自身数据；去掉 `os_malloc(size)` 后窗口大小不再受 HSRAM 堆限制，且窗口只剩
CPU 单 master（符合 API 约束）。

**前提（均由 log 逐条确认，实验有效）**

| 项 | 实测 |
| --- | --- |
| cover 窗口 | `wt task: CPU write-only loop on [64c00000-64c80000] psram1` → PSRAM1，512KB |
| HAL 编程正确 | 本版已含修复：`cfg_start = (start - data_base) >> 5`、`disable_write_through_area` 按芯片作用域 |
| 任务栈位置 | `ss_w0=[64d1bc08-64d27c08]`、`ss_w1=[64d27c30-64d33c30]`、`ss_w2=[64d33c58-64d3fc58]`、`ss_w3=[64d40ca8-64d4cca8]`，TCB `64d0e7xx` → 全在 `AP_PSRAM_HEAP`(0x64D08000+640KB)，**同颗 PSRAM1** |
| 地址不重叠 | cover `0x64c00000-0x64c80000` vs 栈 `0x64d0xxxx+` ✓ |
| 真并发 | `psram_wt` 在 **ap1**、`stack_stress` 在 **ap0**，双核同时跑 |

**结果**：`stack_stress` 从 rounds=6 跑到 **rounds=81、errors=0**（约 80s），期间同芯片
持续直刷写，**无 mismatch、无 assert、无 MemFault**；`wt_stop` 后干净停止。

→ **在“单个、按正确地址编程、静态配置的 PSRAM1 cover + 同芯片并发 CPU 写”条件下，
窗口外任务栈不会被破坏。§12.6 那条“凡与任务栈同芯片即禁止开 cover”的外推不成立。**

**本实验未覆盖的变量（因此不能据此宣布原始崩溃与 cover 无关）**

1. **写入 master 不同**：原始故障是 **GPU** 写 PSRAM1 cover 窗口；本实验是 **CPU** 写。
2. **通道共享与动态重编程**：崩溃时是**两个 cover 并存**且共用 `hal_id=0`——GPU 池
   `area=4`（PSRAM1）与解码 `area=0`（PSRAM0）；且解码 cover 在运行中被反复重编程
   （log 可见 `cnt=1→4`、范围持续增长）。本实验只有一个静态 cover。
3. **带宽与负载**：原始为整条视频流水线（decode+GPU+DPU+网络）；本实验仅两个测试任务。

**对 §12.3 机制假设的修正**：曾设想 HAL 的 “`cfg_start/cfg_end` 未减 `data_base`”
（Bug B）会把窗口错编到任务栈上，从而解释崩溃。但算术上
`0x64000000>>5 = 0x03200000 = 100 × 0x80000`、`0x60000000>>5 = 0x03000000 = 96 × 0x80000`
**均为整除**，即只要 cover_start 字段是 19 位（16MB/32B 所需最小宽度），硬件截断就会
自动剥掉 `0x60/0x64` 映射头，Bug B 等于无害。该字段实际宽度未能从 LL 头确认，故此条
只作为“该假设很可能不成立”，不作结论——但已不宜作为主要解释。

**当前最可能的根因方向（待验证）**：不是“同芯片共处”，而是 **cover 通道跨芯片共用
`hal_id` + 运行中动态 disable/重编程**（Bug A 类）导致窗口一度处于错误/不一致状态，
叠加 GPU 作为写 master。下一步最直接的判别实验见 §12.9。

### 12.9 下一步判别实验

在 **HAL 已修复** 的当前代码上，重新打开 GPU 输出池 cover（`comp_out_pool_cover_enable`）
并跑真实视频流水线：

- 若**不再** assert → 原始崩溃归因于 HAL 的通道/重编程缺陷，已被修复；“同芯片禁止”规则
  可正式废除，GPU 池 cover 可重新评估启用（但注意 §12.10 已证 36fps 不依赖它）。
- 若**仍然** assert → 根因与 GPU-as-master 或双 cover 共用 `hal_id` 相关，需进一步把
  解码 cover 挪到不同 `hal_id`（避免 `area=0` 与 `area=4` 同 `hal_id`）后再测。

### 12.10 §12.9 实验已执行：HAL 修复不足以消除 assert（2026-09-16 14:20 log）

重新打开 GPU 池 cover（PSRAM1）后跑真实流水线，约 5s 复现死机：

| 项 | 实测 |
| --- | --- |
| 故障类型 | `UsageFault / INVSTATE`（取指时 EPSR.T 位为 0） |
| 核 | `core=3`（AP1） |
| 任务 | `tcp/ip`，PSP=`0x64d0b0c8`，落在其自身 PSRAM1 栈内 |
| 异常帧 | xPSR=`0x40000000`——**缺 Thumb T 位**，即被保存的任务上下文被改写 |

→ 不是“取指到非法地址”，而是**栈上保存的上下文（xPSR/LR）被破坏**后 `POP {PC}` 回去
执行。与 §12.5 的 poisoned r4-r11、MemFault 属同一族：PSRAM1 上任务栈内容被污染。

因此：HAL 的地址计算与芯片作用域缺陷虽然真实存在且已修复，但**不是**（至少不是唯一）
原因。剩余两个变量是 §12.8 未覆盖的那两条：GPU 作为写 master、以及双 cover 共用
`hal_id`（GPU `area=4` on PSRAM1 与解码 `area=0` on PSRAM0，且解码侧运行中重编程）。

### 12.11 单变量实验：只留一个 cover

已改代码（本次提交）：

- `projects/intercom_indoor_display/ap/config/bk7259_ap/defconfig`：
  `CONFIG_H264D_RECON_WRITE_THROUGH` 改为 not set（PSRAM0 不再开窗口，解码 cover 不再
  被运行中反复 disable/重编程）；`CONFIG_PSRAM_WRITE_THROUGH=y` 保留。
- `components/smart_intercom/src/doorbell_display_compositor.c`：GPU 池 cover
  （PSRAM1，`comp_out_pool_cover_enable`）保持打开。

已核对生成配置与产物：`bk7259_ap/config/config:1957 = # CONFIG_H264D_RECON_WRITE_THROUGH
is not set`，ELF 中解码 cover 相关字符串消失、`out pool cover: ...` 仍在。

判别逻辑：

- 若**不再** assert → 根因是**双 cover 并存 / 跨芯片共用 `hal_id` + 运行中重编程**，
  修复方向是给两个窗口分配独立 `hal_id` 并避免动态重编程，而非禁止 cover。
- 若**仍然** assert → 双 cover 被排除，唯一剩下的变量是 **GPU 写该窗口**本身（burst
  形态/非 32B 整行写/与 DPU 读并发），修复方向在 GPU 侧对齐与 cover 窗口粒度。

无论哪一支，36fps 都不依赖该 cover（§12.10 前置结论），所以产线配置可继续保持关闭。

### 12.11.1 §12.11 实验已执行：双 cover 被排除（2026-09-16 14:45 log）

固件 banner `build time => Sep 16 2026 14:40:55` 与本次重编 AP ELF 一致。实验有效：

| 项 | 实测 |
| --- | --- |
| 解码 cover | 全程无 recon/DPB cover 日志 → PSRAM0 窗口未开 |
| GPU 池 cover | `out pool cover: write-through 0x6467fb40-0x64922c40 slots=3 area=4`（64B 对齐） |
| 窗口 vs 栈 | cover 止于 `0x64922c40`，崩溃栈在 `0x64d29d70+`，中间约 4.2MB，**不重叠** |
| 流水线 | GOP loop 已武装；死机前 `h264d+gpu_wait ~33.9fps`，DPU `fps 33 / rps 36` |
| 死机时刻 | cover 打开约 9.4s 后（tick 65120 → dump AON-RTC 74.53s） |

故障现场（唯一非零 CFSR 的核）：

| 项 | 值 |
| --- | --- |
| `@PRIMARY_EXCEPTION` | `reason=0x12 core=3 pc=0 lr=0 sp=0x64d2a920 CFSR=0x1` |
| Dump-reason | MemFault |
| CFSR | `0x1` = MMFSR.IACCVIOL（取指 MPU/权限失败）；MMARVALID=0，MMFAR 无效 |
| PC / LR | 均为 `0`；硬件异常帧（PSP=`0x64d2a920`）里也是 `pc=0 lr=0 xpsr=0x60000000` |
| xPSR | `0x60000000`：IPSR=0（Thread），**T 位=0** |
| ER | `0xfffffffd`：Thread + PSP + 基本帧 |
| 任务 | `ntwk_tcp_vid_cli`（`stack=[64d29d70-64d2a970]`，PSPLIM=`0x64d29d70` 完全吻合） |
| 毒化痕迹 | `r9=0x09090909 r10=0x10101010 r11=0x11111111` |

与 §12.10 同族：PSRAM1 上网络任务的保存上下文被改写，随后 `POP {PC}` / 取指落到 `0`。
这次受害者从 `tcp/ip` 换成了 **下行视频 TCP 客户端**，仍是 `CONFIG_TASK_STACK_IN_PSRAM`
放在 PSRAM1 上的任务，不是 GPU/解码任务本身。

**判定**

- 双 cover / `hal_id` 共享 / 解码侧动态重编程 → **证伪**（本次只有 `area=4` 一个窗口）。
- HAL 地址计算修复 → **不足以**消除该崩溃（本固件已含修复）。
- “CPU 往同芯片 cover 窗口写就会毁掉窗外栈” → 已被 §12.8 **证伪**。
- 仍成立的必要组合：**PSRAM1 上对 GPU 输出池开 cover + 真实 GPU（及 DPU）访问该窗口**。
  API 要求窗口内单 master、整 32B 行写；GPU 压缩写 + DPU 读同一块缓冲，这两条都不满足。

**下一步最小判别（已执行，见 §12.11.2 / §12.13）**

保持 GPU 池 cover，关掉 `CONFIG_TASK_STACK_IN_PSRAM`（栈回到 SRAM）。`QUEUE_IN_PSRAM`
当时未动。

### 12.11.2 栈进 SRAM 后仍 assert（2026-09-16 15:23 log）

实验有效：全部 `create ... stack=[2c1xxxxx]`；cover 仍开
`0x64760bc0-0x64a03cc0 area=4`；banner `Sep 16 2026 15:20:37`。视频跑约 122s / 3859 帧
后主动 `BK_ASSERT`，不是 9s 级 MemFault。

| 项 | 实测 |
| --- | --- |
| Dump-reason | Assert at `wdrv_rxdata_process:162` |
| 任务 | `wdrv_thread`，PSP=`0x2c1b73c0`，PSPLIM=`0x2c1b63f0`，SRAM 栈未溢出 |
| 反汇编 | `WDRV_STATS_SMP_DEC(tx_alloc_num)`：`tx_alloc_num==0` 仍 DEC |
| `r6..r11` | `0x06..0x11` 重复字 = FreeRTOS port 初始填充，**不是**污染证据 |

→ “只有 PSRAM1 任务栈会被打坏”不成立。队列仍在 PSRAM（`CONFIG_QUEUE_IN_PSRAM=y`），
WDRV 的 `io_queue` 与计数路径仍可能被同芯片小粒度写打坏。

### 12.13 单变量：队列也进 SRAM

`CONFIG_QUEUE_IN_PSRAM` 改为 not set；GPU cover、解码 cover、SRAM 栈保持上一轮。

### 12.13.1 §12.13 实验已执行：队列进 SRAM 后挂测稳定（2026-09-16 15:46 log）

实验有效（无 dump banner，靠运行时证据对齐）：

| 项 | 本次 15:46 | 对照 15:23（栈 SRAM、队列仍 PSRAM） |
| --- | --- | --- |
| GPU 池 cover | `0x64760bc0-0x64a03cc0 slots=3 area=4` **相同** | 同 |
| 解码 cover | 无 | 无 |
| 任务栈 | 全 `2c1xxxxx` SRAM | 全 `2c1xxxxx` SRAM |
| 队列 | `CONFIG_QUEUE_IN_PSRAM` not set → SRAM | `=y` → PSRAM1 |
| GOP | 33104 武装 | 31813 武装 |
| 结果 | 无 `PRIMARY_EXCEPTION` / 无 `Dump-reason` / 无 `wdrv` assert | tick 152619 `Assert at wdrv_rxdata_process:162`（`tx_alloc_num==0`） |
| 时长 | cover 打开后 **~546s**（末行 tick 578047），heartbeat 到 566597 | cover 后 **~122s** 即崩 |
| 帧数 | DPU layer0 total **17507**，末段仍 `h264d ~32.3fps` / DPU `fps 32 rps 36` | 崩前 ~3859 帧、~32.1fps |

唯一有意改动是队列位置。GPU 窗口地址与 15:23 **逐字节相同**，因此不是 cover 没开、也不是窗口碰巧变了。

**判定**

- 15:23 的 `tx_alloc_num==0` 与更早的 PSRAM 栈 MemFault 是**同一机制的不同受害者**：
  GPU 在 PSRAM1 开 write-through 后，同芯片上 CPU 的小粒度写（栈、RTOS 队列控制块/队列项）会被破坏。
- 栈先搬走 → 崩溃从“9 秒级上下文损坏”变成“2 分钟级 WDRV 计数 assert”（队列还在 PSRAM）。
- 队列也搬走 → 同一 cover + 同一 GOP 负载下跑完 9 分钟无 assert。
- §12.8 的 CPU-only cover 仍然说明：**不是**“同芯片任意 cover 都会坏”，而是 **GPU（± DPU）当窗口 master** 时，窗外的 CPU 写会中招。

**残留风险（未排除）**：PSRAM1 上其它 CPU 数据结构仍可能被打（例如 `CONFIG_NTWK_USE_PSARM_MEM`、lwip pbuf、业务缓冲）。本次只证明 RTOS 栈+队列离开 PSRAM1 后，已知那两条死法消失。另：本次启动有
`DL FLEXA ring: SRAM5/6 low ... fall back to HSRAM`，栈+队列挤占 SRAM 的副作用，与稳定性结论独立。

**产线建议不变**：GPU 池 cover 关闭；36fps 不依赖它。不要为了开 cover 把任务栈/队列长期留在 SRAM——那是实验条件，不是产品默认。

### 12.14 参考工程对照：write-through 配置本身没问题，是内存布局冲突（更正 12.10/12.12 的机制表述）

对 `bk_avdk_smp_release_4.0.1_simple/projects/multimedia/h264d_gpu_display_example` 逐项核对：

| 事实 | 证据 |
| --- | --- |
| 参考的 GPU 显示帧池 cover 落 **PSRAM0**（UNCODED `0x60000000`） | `docs/psram_write_through.md:18/72`、`h264d_gpu_display_gpu.c:217` `bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,...)` |
| 室内机把同一用途 cover 落 **PSRAM1**（CODED） | `doorbell_display_compositor.c` `COMP_OUT_HEAP=MEM_SLAB_HEAP_CODED` |
| 分块方式：扫所有 slot 取 min-start/max-end **合成 1 块**，占 1 条通道；不是每 buffer 一块 | 两边 `*_cover_enable()` 同逻辑 |
| 覆盖窗口的写入方是 **HPDMA**（整块对齐搬运），不是 GPU core | `bk_gpu_ctlr_default.c:809-810`（GPU 渲染目标是 SRAM 条带 `.address=SOC_SRAM_PERI_ADDR`）、`:725-726`（HPDMA `src=SRAM 条带 → dst=dpu_frame_buffers`） |
| 读取方是 **DPU**，单写(HPDMA)单读(DPU)，无并发多写 | 同上 + DPU 刷屏路径 |
| 参考工程 `TASK_STACK_IN_PSRAM=y`，`PSRAM_HEAP @ 0x64e60000` = **PSRAM1**，且 PSRAM1 上另有 recon cover，仍稳定 | `h264d_gpu_display_example/.../app.map:15027`、`defconfig:37/38` |

**据此更正**：

- 收回 §12.10/§12.12 的“GPU 当 master、非 32B 整行写违反 API”表述——**不成立**。窗口由 HPDMA 整块对齐写、DPU 读，是 write-through 的理想用法。
- 收回“cover 与任务栈同颗 PSRAM 即冲突”——**不成立**。参考工程 recon cover 与栈同在 PSRAM1 且稳定。
- **write-through 配置本身正确**（地址/对齐/单块合成/单 DMA 写+DPU 读，与被验证的参考工程一致）。崩溃是**内存布局冲突**：把“显示输出这条高带宽 cover（HPDMA 写 + DPU 读，每帧整屏）”与 CPU 任务栈/队列放到了同一颗 PSRAM1。

**唯一区分变量**（其余全相同）：

| 配置 | 显示输出 cover | 栈/队列 | 结果 |
| --- | --- | --- | --- |
| 参考工程 | PSRAM0 | PSRAM1（+recon cover） | 稳定 |
| 室内机(崩) | PSRAM1 | PSRAM1 | ~9s/~122s 崩 |
| 室内机(现) | PSRAM1 | SRAM | 9 分钟稳定 |
| §12.8 纯 CPU 写 | PSRAM1 | PSRAM1 | 80s 不崩 |

→ 触发需要“显示输出 cover 的 HPDMA 写 + DPU 读 高带宽流量” **叠加** “同颗 PSRAM1 上有 CPU 小粒度写(栈/队列)”。recon cover(§参考)与纯 CPU 写(§12.8)都不满足这个叠加，故不崩。硅层机制(写缓冲/行刷与 DPU 读的相互作用如何波及窗外 CPU 行)未用逻辑分析仪坐实，暂记为强相关。

**两条稳定路线**（择一）：

1. **推荐**：栈/队列留 PSRAM1，把 GPU 输出池 cover 挪到 **PSRAM0**（`COMP_OUT_HEAP` 改 `MEM_SLAB_HEAP_UNCODED`），对齐参考工程。保住 PSRAM 栈的内存收益。
2. cover 留 PSRAM1，栈/队列放 SRAM（现版，已验证 9 分钟稳定，但吃 SRAM，出现过 `SRAM5/6 low → HSRAM`）。

### 12.15/12.16 交换实验：cover 换成解码器 master，仍崩，且与 §12.10 同一 assert（2026-09-17 9:58 log）

交换配置(本次固件 `build time => Sep 17 2026 09:56:19`)：

- GPU 输出池 → PSRAM0（`COMP_OUT_HEAP=MEM_SLAB_HEAP_UNCODED`），**cover 关**（无 `out pool cover` 日志）；
- 解码 recon → PSRAM1（`h264_decode_mem.c` 改 `MEM_SLAB_HEAP_CODED`），**cover 开**（`CONFIG_H264D_RECON_WRITE_THROUGH=y`）；
- 栈 + 队列 → 回 PSRAM1。

实测生效：`dec cover: recon write-through 0x64...-0x64... area=4 psram1`（cnt=1→3 增长重编程）；栈全在 `0x64d0xxxx`。

**结果：约 7.7s 后崩，且与 §12.10（14:20，GPU cover 在 PSRAM1）逐项相同：**

| 项 | 本次 9:58 | §12.10（14:20） |
| --- | --- | --- |
| 类型 | UsageFault，CFSR=`0x20000` INVSTATE | 同 |
| 核 | core=3(AP1) | 同 |
| 任务 | `tcp/ip`，PSP=`0x64d0b0c8` | `tcp/ip`，PSP=`0x64d0b0c8`（同址） |
| xPSR | `0x60000000`，缺 T 位（保存上下文被改写） | 同 |

**关键更正（推翻 §12.14 的收窄）**：

- cover 的写 master 从 GPU/HPDMA 换成解码器，受害者与死法**完全不变** → 根因**不是** “GPU 当 master”、也**不是**显示/DPU 这条特定路径。§12.14 “write-through 配置本身没问题、崩溃仅是显示输出路径 + 布局”这一收窄**过窄**。
- §12.14 用参考工程 `h264d_gpu_display_example` 做的“recon cover + 栈同在 PSRAM1 仍稳定”**不成立作为背书**：该 example 跑本地码流，**没有 Wi-Fi/lwip/tcp-ip 网络任务负载**，从未在“网络任务栈在 PSRAM1 + PSRAM1 有活动 cover”下运行。本实验补上了这个变量 → 崩。
- 与 §12.8（纯 CPU 写 cover 80s 不崩）合看：触发需要 **PSRAM1 上有活动 write-through cover + 同颗 PSRAM1 承载频繁被抢占的网络任务栈 + 真实并发流量**；cover 由谁写(GPU/HPDMA/解码器)无关。

**收敛后的规律（高置信）**：只要 **write-through cover 与 RTOS(尤其网络)任务栈同处 PSRAM1**，真实负载下就会打坏被抢占任务的保存上下文。把二者分开即稳：§12.13.1（cover 留 PSRAM1、栈+队列搬 SRAM）9 分钟无 assert。

**据此的可行方案**（择一，均需让 cover 与任务栈不同芯片/介质）：

1. 栈/队列在 PSRAM1，则任何显示/解码 cover 都放 **PSRAM0**（且 PSRAM0 上不要放任务栈）；
2. cover 放 PSRAM1，则任务栈/队列放 **SRAM**（§12.13.1 已验证，代价是吃 SRAM）；
3. 产线：**不开 cover**，36fps 不依赖它（最稳、零代价）。

### 12.25 主动探针 + 正常模式实验（2026-09-17 16:01 / 16:31 log）

在 `ap_main.c` 加了 CLI 压测探针 `pstress`（周期从非 cache `AP_PSRAM_HEAP` 申请一批小块、
逐字节写 pattern、跨帧持有、校验后释放），并把 `DL_TEST_LOOP_GOP` 默认改 0（正常模式，
走真实网络下行）。两轮：

| 轮次 | 模式 | cover | cJSON heap | pstress | 结果 |
| --- | --- | --- | --- | --- | --- |
| 16:01 | 正常(真实网络) | ON | SRAM | **重** `128×512 hold50` | **Watchdog @635s**（`vListInsert` 死循环，`vTaskPlaceOnEventList`）；探针 10560 轮 `corrupt=0` |
| 16:31 | 正常(真实网络) | ON | 非 cache PSRAM(金丝雀) | **轻** `128×9 hold200` | **35 分钟稳定**，无 assert/watchdog，探针 `corrupt=0` |

**结论更正/收敛：**

1. **"cover 广泛打坏非 cache PSRAM" 被证否**：主动探针 + cJSON 金丝雀两个检测点在非 cache
   PSRAM 上，正常模式 35 分钟 `corrupt=0`、无堆 assert。§12.21/§12.22 的推广彻底作废。
2. **16:01 的 watchdog 归因于探针重负载**，与 cover 无关：同 cover/同模式，重负载(128×512
   hold50)→ 635s watchdog；轻负载(128×9 hold200)→ 35 分钟稳。watchdog 随 pstress 负载
   出现/消失，是重负载暴露的 FreeRTOS SMP 链表竞态/堆争用（`vListInsert` 死循环）。
3. **崩的复现分界是"GOP 测试模式 vs 正常模式"**：此前 cJSON/栈/队列的崩全部发生在
   `DL_TEST_LOOP_GOP=1`（本地循环回放、忽略网络）；**正常模式（真实网络）cover 开目前两次
   均稳**（§12.24 前置 15-35 的短测 + 本次 35 分钟）。§12.24 的 cover A/B 亦在 GOP 测试模式内。
   → 更准确表述：**"GOP 测试回放 + cover" 的时序叠加触发崩，正常模式 cover 开未复现**；
   GOP 回放路径产线不发布。

**产线建议（更新）**：正常模式 cover 开 35 分钟稳是正向信号但不足以拍板；启用 cover 前需
**正常模式长稳**（多次拨号/挂断、断连重连、连续数小时，cJSON 金丝雀 + 低负载 pstress 常开监控）。
否则维持最稳方案：cover 关（36fps 不依赖）或 cover 挪 PSRAM0。

### 12.26 GOP 模式 A/B 判别（2026-09-17 17:18 log）：崩=GOP 回放+cover,正常模式不崩

切回 `DL_TEST_LOOP_GOP=1`（GOP 测试回放），其余与 16:31 那次 35 分钟稳定的正常模式**完全相同**
（cover ON、cJSON 非 cache PSRAM 金丝雀、栈/队列 cache 堆、`pstress 128×9 hold200`、UAF 审计）。

结果：**~30 分钟(1792s) Watchdog**，`pc=vListInsert / lr=vTaskPlaceOnEventList`（event list
死循环），卡住任务栈在 **cache PSRAM** `0x64d6c858`，r8-r11=`08/09/10/11`（FreeRTOS 栈初始
填充，疑似上下文被改写）。pstress 全程 `corrupt=0`、cJSON 金丝雀无堆 assert（**非 cache 未被打**）。

| 运行 | 模式 | cover | pstress | 结果 |
| --- | --- | --- | --- | --- |
| 16:31 (§12.25) | 正常(真实网络) | ON | 128×9 hold200 | 35min 稳 |
| 17:18 (本节) | **GOP 测试** | ON | 128×9 hold200(同) | ~30min watchdog |

**唯一差别是 `DL_TEST_LOOP_GOP`** → **判定：崩由 "GOP 本地回放 + cover" 时序叠加触发；正常模式
（真实网络）cover 开不复现。** GOP 回放是测试专用路径，产线不发布。

**机制收敛**：
- 非 cache PSRAM 未被打（cJSON 金丝雀 + pstress 探针多次多轮 corrupt=0）；
- GOP 模式下被打坏的是 **cache PSRAM 的 RTOS 结构**（event list / 任务上下文）→ vListInsert 死循环；
- pstress（频繁 block/unblock）放大 event-list 活动，把 GOP 模式的失败从 cJSON 堆 assert（§12.21）
  推成 vListInsert watchdog，但根因同为 "GOP 回放+cover 破坏 PSRAM 内容"。

**产线结论（终版倾向）**：
- 实际风险低——崩仅在测试专用 GOP 回放路径复现，正常模式 cover 开已 35min 稳；
- 但启用 cover 前建议补正常模式数小时长稳 + 多次拨号/断连；
- 零风险选择不变：**cover 关**（36fps 不依赖）或 **cover 挪 PSRAM0**。

### 12.12 结论（以 §12.16 为准）

- **write-through 配置本身正确**：地址/32B 对齐/单块合成，编程无误。
- **崩溃是内存布局冲突，与 cover 的写 master 无关**：只要 **write-through cover 与 RTOS(尤其网络)任务栈同处 PSRAM1**，真实负载下就打坏被抢占任务的保存上下文（INVSTATE / MemFault / 或队列在 PSRAM1 时的 `wdrv tx_alloc_num==0`）。
  1. 栈+队列在 PSRAM1 + GPU cover 在 PSRAM1 → 崩（§12.5/§12.10/§12.11.x）；
  2. 栈+队列在 PSRAM1 + 解码 cover 在 PSRAM1（GPU cover 关）→ **同样崩，同一 assert**（§12.16）；
  3. 栈+队列移到 SRAM（cover 仍 PSRAM1）→ 9 分钟无 assert（§12.13.1）；
  4. 纯 CPU 写 cover、无并发重流量 → 不崩（§12.8）。
- **已被证伪**（更正早期表述）：
  - “GPU 当 master、非 32B 整行写违反 API”——换成解码器 master 仍崩（§12.16）；
  - “仅显示输出/DPU 路径才触发”——解码 recon cover 同样触发（§12.16）；
  - “参考工程背书 recon cover+栈同 PSRAM1 安全”——该 example 无网络负载，不构成背书（§12.16）；
  - “双 cover / 跨芯片共用 `hal_id` / 解码动态重编程是必要原因”——§12.11.1。
- **推荐修复**：让 cover 与任务栈**不同芯片/介质**——栈在 PSRAM1 则 cover 放 PSRAM0；或 cover 在 PSRAM1 则栈放 SRAM；产线最稳是**不开 cover**（36fps 不依赖）。
- **仍未定论**：活动 cover 如何波及同芯片窗外 CPU 行（写缓冲/行刷别名），硅层机制未用逻辑分析仪坐实。
- 置信度：布局冲突因果链（cover ⊕ 任务栈同芯片）= 高；硅层机制 = 中。

### 12.17 关键变量补全：PSRAM 任务栈默认是 non-cacheable（更正“机制未定”）

之前一直把受害者笼统写成“PSRAM1 上的 CPU 小粒度写”，漏了最关键的一条属性——**这些栈是
non-cacheable 的**。核对 SDK：

| 事实 | 证据 |
| --- | --- |
| `AP_PSRAM_HEAP` 的 MPU 属性 = attr 1 = **non-cacheable**；`AP_PSRAM_CACHE_HEAP` = attr 5 = **L2 write-back（注释明确 used for AP task stacks）** | `ap/middleware/soc/bk7259_ap/mpu_cfg.c:107-115`；生成的 `ram_regions.h`（region1 ATTR=1、region2 ATTR=5）；MAIR `ARM_MPU_ATTR(0xf,0x4)` |
| 任务栈/TCB 分配走 `task_stack_malloc → psram_cache_malloc` | `freertos_smp_v2p0/.../freertos_tasks_c_additions.h:184-192` |
| **无 `AP_PSRAM_CACHE_HEAP` 时 `psram_cache_malloc` 回退为 `psram_malloc`（即 `AP_PSRAM_HEAP`，non-cacheable）** | `ap/include/os/mem.h:190-193`（`#else #define psram_cache_malloc psram_malloc`） |

**推论**：旧 `intercom_indoor_display/partitions/bk7259/ram_regions.csv` 没有
`AP_PSRAM_CACHE_HEAP` → 任务栈全部落在 non-cacheable 的 `AP_PSRAM_HEAP`。每次上下文
保存/恢复都是**直连控制器的非 cache 小写**；同颗 PSRAM1 有 write-through 整行刷写时，
两者在写路径相互破坏 → 被抢占任务现场被改写。这条把 §12.16 的“同芯片冲突”落到了具体机制：
**冲突的必要条件是“非 cache 栈”，不是“同芯片”本身**。也解释了：
- “栈搬 SRAM”能稳（SRAM 是 normal cached，§12.13.1）；
- CPU-only `wt_start`（§12.8）不崩：它没有并发的高带宽 cover 整行刷写流量。

**已做的修复（本次）**：同步 `video_intercom` 的 `ram_regions.csv`——新增
`AP_PSRAM_CACHE_HEAP 0x40000`（attr 5 / L2 WB），`AP_PSRAM_HEAP` 调整为 `0x61000`，
删除 `AP_PSRAM_NOCACHE_HEAP`。生成确认：`AP_PSRAM_HEAP @ 0x64D07000`、
`AP_PSRAM_CACHE_HEAP @ 0x64D68000 ATTR=5`。此后任务栈/TCB 从 **L2 write-back cache 堆**
分配，栈写被 cache 吸收、按整行回写，预期可与同芯片 cover 的行刷共存。

### 12.18 §12.17 复测：栈 cache 生效，受害者转移到非 cache 队列（2026-09-17 11:01 log）

固件 `build time => Sep 17 2026 10:58:58`。配置：GPU cover 在 PSRAM1（`out pool cover
0x64760bc0-0x64a03cc0 area=4`）、解码 cover 回 PSRAM0（`dec cover ... area=0 psram0`）、
栈走 `AP_PSRAM_CACHE_HEAP`（L2 WB）、队列仍 `CONFIG_QUEUE_IN_PSRAM=y`（非 cache）。

**结果一（栈 cache 修复有效）**：
- 无 PSRAM 栈 INVSTATE / 上下文改写；
- 运行时间从 ~8s 拉到 **~174s**（GOP 38433 → 崩 214279），全程 `h264d ok 180f skip 0` 稳定 36fps。

**结果二（新受害者 = 队列）**：约 174s 后崩，与之前完全不同：

| 项 | 实测 |
| --- | --- |
| 现场 | `reason=0x12 core=2` MemFault，CFSR=`0x82`（DACCVIOL+MMARVALID），**MMFAR=0x4** |
| 上下文 | xPSR IPSR=`0x12`、ER=`0xfffffff1` → **ISR/handler + MSP**，SP 在 SRAM 主栈 |
| 调用链 | `mailbox RX ISR → wdrv_recv_buffer → wdrv_msg_sender → rtos_push_to_queue → xQueueGenericSendFromISR(queue.c:1320) → xTaskRemoveFromEventList(tasks.c:4259/4261)` |
| 坏对象 | `pxUnblockedTCB = listGET_OWNER_OF_HEAD_ENTRY(pxEventList)` 得到近 NULL，`listREMOVE_ITEM` 解引用 → MMFAR=0x4 |
| 队列位置 | r1/r2=`0x64d0836c`、r12=`0x64d08768` → 落在 `AP_PSRAM_HEAP [0x64D07000,0x64D68000)` = **非 cache PSRAM1** |

→ 队列（`Queue_t` + 内嵌等待链表，`queue.c:567 psram_malloc` 分配）仍是**非 cache PSRAM1**，
被同芯片 GPU cover 的行刷打坏其 event list 节点。**机制与栈那类同源**（非 cache 小结构 ⊕
同芯片活动 cover），只是 §12.17 的栈修复把受害者从“栈上下文”推到了“队列链表”。

**据此已做**：本项目 defconfig 关闭 `CONFIG_QUEUE_IN_PSRAM`（队列回 SRAM，normal cached）。
组合变为：栈=PSRAM cache 堆、队列=SRAM、GPU cover=PSRAM1。

### 12.19 复测通过：栈(PSRAM cache) + 队列(SRAM) + GPU cover(PSRAM1) 稳定

栈走 PSRAM L2 cache 堆、队列回 SRAM、GPU cover 仍在 PSRAM1，跑 GOP 长测**无 assert**。
两个非 cache 受害者（栈、队列）都移出“非 cache PSRAM1”后，同芯片 cover 不再打坏任何东西。

→ **根因正式坐实**：触发是“**non-cacheable 的 CPU 小写结构 ⊕ 同芯片活动 write-through
cover ⊕ 真实并发流量**”。与 cover 的写 master（GPU/HPDMA/解码器）无关，与地址是否重叠无关。
正解：让与活动 cover 同芯片的 CPU 小写结构**走 cache**或**移出该芯片**，无需牺牲 cover。

### 12.20 进一步验证：队列也放 PSRAM，但过 L2 cache

若想 `CONFIG_QUEUE_IN_PSRAM=y`（省 SRAM），只需让队列也走 cache 堆。已做：

- SDK `freertos_smp_v2p0/FreeRTOS-Kernel/queue.c:567`：`psram_malloc` → **`psram_cache_malloc`**，
  使 `Queue_t` + 队列存储 + 内嵌 event list 落在 `AP_PSRAM_CACHE_HEAP`（L2 WB）。
- 本项目 defconfig 重新打开 `CONFIG_QUEUE_IN_PSRAM=y`。

预期：队列不再是非 cache，§12.18 的 `xTaskRemoveFromEventList` MemFault 应消失，
栈+队列均在 PSRAM1（都过 cache）+ GPU cover 在 PSRAM1 也能长测稳定。**待复测确认。**

### 12.21 §12.20 复测：队列已过 cache，但受害者再转移到通用堆上的 cJSON（2026-09-17 13:38 log）

固件 `build time => Sep 17 2026 13:36:18`。栈、队列均落在 `AP_PSRAM_CACHE_HEAP`
（0x64D68000+，log 中 `create` 栈地址为 `0x64d7xxxx`），GPU cover 仍在 PSRAM1
（`area=4`），解码 cover 在 PSRAM0。跑 ~205s（GOP 44803 → 249290）稳定 36fps 后 assert。

**queue.c 改动确认生效**：队列不再触发 §12.18 的 `xTaskRemoveFromEventList` MemFault。

**新受害者 = 通用堆上的 cJSON**：

| 项 | 实测 |
| --- | --- |
| 现场 | `reason=0xe core=2` Assert（非 MemFault），日志 `Invalid heap debug metadata at 0x64d1fb08` |
| 调用链 | `prvTimerTask → db_rpc_send_json(doorbell_jsonrpc.c:62) → cJSON_Delete → bk_heap_free_impl → bk_heap_overflow_check → bk_heap_debug_get_tag_bounds:390` |
| 坏对象 | `0x64d1fb08` ∈ `AP_PSRAM_HEAP [0x64D07000,0x64D68000)` = **非 cache 通用堆** |
| 性质 | 心跳 RPC 周期 new/delete 的 cJSON 落在通用 `psram_malloc` 堆，堆块 tag 被同芯片 cover 打坏 |

→ 栈搬 cache→队列中招；队列搬 cache→通用堆 cJSON 中招。**打地鼠**证实：只要 cover 在 PSRAM1，
该芯片上**任何非 cache 的 CPU 小写结构**（通用堆里的 cJSON、以及潜在的 lwip/`NTWK_USE_PSARM_MEM`/
`LWIP_MEM_LIBC_MALLOC_USE_PSRAM` 等）都是潜在受害者；而通用堆里有大量必须非 cache 的 DMA 面向
buffer，不可能全搬 cache。

### 12.22 结论更正（§12.21 的“打地鼠/任意非 cache 受害”被反例证伪）

**反例（用户指出）**：§12.13.1（队列在 SRAM）那次，cJSON 同样在非 cache PSRAM1、GPU cover
同样在 PSRAM1，却跑了 578s 无事（比本次崩的 249s 更久、十几次心跳 new/delete）。若“cover 打坏
PSRAM1 上任意非 cache 结构”成立，那次 cJSON 早该崩。故 §12.21 的推广**不成立，收回**。

**两类失败需分开**：
- 栈 / 队列：硬件 MPU/Mem/UsageFault，现场 INVSTATE、上下文寄存器被垃圾覆盖——搬 cache 后消失
  （§12.17/§12.20），这部分实。
- 本次 cJSON：软件堆完整性 assert（`bk_heap_debug_get_tag_bounds`，`MEM_DEBUG=y`），堆块 tag 坏，
  可能来自任意越界/UAF/竞态，**未证明与 cover 相关**。两版间除“栈/队列搬 cache”外还有变量：
  `ram_regions` 里 `AP_PSRAM_HEAP` 基址/大小变了（布局重排可能暴露潜伏堆 bug）、解码 recon cover
  本版为开。

### 12.23 结合 SDK 统一 cache/PSRAM 策略的定性（关键判据）

SDK 已统一：① L2 ON、L1 D-Cache OFF；② `psram_malloc/psram_zalloc` 默认 **non-cacheable**，
供网络/音视频/显示/AI/DMA 业务 buffer 用；③ `AP_PSRAM_CACHE_HEAP`（cacheable）**仅供 OS
task/queue 创建路径内部使用，业务模块不得直接用 `psram_cache_malloc`**；④ 禁止把任务栈上变量
交给 DMA/CP/GPU/NPU/WiFi/BT/display/codec 等第三方 master 异步访问。

据此重定性：
- §12.17/§12.20 把**栈、队列**放进 `AP_PSRAM_CACHE_HEAP`（L2 WB）**符合策略③**，是设计用途，
  保留；对应的栈/队列崩是“用对 cache 堆”后消失。
- §12.21/§12.22 想把 **cJSON（业务 buffer）搬 cache** 的方向**违反策略②/③，作废**：业务 buffer 必须
  non-cacheable，cJSON 非 cache 是正确状态。
- 因此本次 cJSON 堆元数据损坏**不应**按“cover 打非 cache”解释，改按两条查：
  (a) 独立堆越界/UAF（被 `ram_regions` 布局变化暴露）；
  (b) 策略④ 的 coherency 违规——是否有代码把**栈上/或 cache 堆内**的地址交给某 master 异步读写
  （栈/队列此版才移入 L2 WB cache 堆，run A 在 SRAM，故此隐患本版才可能暴露）。

**判别实验（建议先做）**：其余不动，仅关闭 GPU cover，长测。
- cJSON assert 仍复现 → 与 write-through **无关**，按 (a)/(b) 查（独立堆 bug 或 stack→master coherency）；
- 不再复现 → cover 仍有牵连，但机制比 §12.21 更挑条件，需解释 §12.13.1 为何不崩。

### 12.24 §12.23 判别实验已执行：关 GPU cover 后 cJSON 崩消失（2026-09-17 14:34 log）

同固件基线，仅 `comp_out_pool_cover_enable` 不调用（GPU cover OFF），其余不动
（栈/队列在 PSRAM cache 堆、解码 cover 仍在 PSRAM0、cJSON 仍非 cache PSRAM1）。
日志无 `out pool cover`、无 `PRIMARY_EXCEPTION`/`Assert`/`Invalid heap`，**跑到 988s
（~16.5 分钟）稳定 36fps**。对照 cover ON 时 cJSON 在 249s/315s 崩。

**结论（干净 A/B，只翻转 cover 开关）**：cJSON 堆损坏**确与 GPU cover（PSRAM1
write-through）相关**，cover 是必要触发条件。**§12.21/§12.22 “cJSON 崩与 cover 无关、
是独立堆 bug” 的推断被推翻，收回。**

**与 §12.13.1 反例的调和**：§12.13.1（cover ON、栈/队列在 SRAM、旧 ram_regions，578s 稳）
说明 cover ON **并非充分**——是否打中某个非 cache 结构、多久打中，取决于**内存布局/时序**。
新布局（栈/队列搬 cache 堆 + ram_regions 改动）使 cJSON 落到会被打中的位置，故 249~315s 中招；
旧布局下恰好没被打中或很久才会。准确表述：

> **GPU cover 开在 PSRAM1 → 破坏该芯片上非 cache 数据；受害者与时机由布局决定。**
> 不是“必打任意非 cache”，也不是“与 cover 无关”。搬 cache 只是换受害者（栈→队列→cJSON），
> 关 cover 才根除。与 §12.8（纯 CPU 写、无 GPU/DPU 流水线、80s 不崩）一致：需真实 GPU
> cover + 流水线才触发。

**硅层机制仍未坐实**：cover 窗口（如 0x64760bc0-0x64a03cc0）并不覆盖 cJSON（0x64d1fbxx），
却能打到它 → 属“write-through 扰动同芯片窗口外写路径”，需逻辑分析仪/寄存器级确认；不影响产线决策。

**产线定论（证据链完整）**：
1. **不开 GPU cover**（36fps 不依赖，本次 965s+ 验证最稳）；或
2. GPU 输出池 + cover 挪 **PSRAM0**（对齐 `h264d_gpu_display_example`），PSRAM1 不再有 cover。
栈/队列进 cache 堆（§12.17/§12.20，合策略③）可保留，但不能作为“PSRAM1 开 cover”的充分保护。

**产线结论（不变，且被策略佐证）**：业务 buffer 必须 non-cacheable（策略②），而 write-through cover
会伤及同芯片非 cache 结构 → 要么**不开 cover**（36fps 不依赖，最稳），要么把 cover 放到**不承载关键
CPU 结构的芯片**；不能靠“把业务 buffer 搬 cache”规避。栈/队列进 cache 堆（§12.17/§12.20）依策略③
保留。

注意（隐患，非本次阻塞）：队列删除路径 `vQueueDelete → vPortFree`（`heap_4.c`）是 **SRAM 堆
free**、不按地址分派；队列从 `psram_cache_malloc` 分配时属 malloc/free 堆不匹配。此为 SDK
既有问题（旧 `psram_malloc` 版同样存在），仅在**删除 PSRAM 队列**时才踩；本工程队列多为初始化
建好长期存活，性能复测不触发。若将来会删 PSRAM 队列，需把释放改为按地址分派（`os_free`）。

---

## 13. 产线最终配置（2026-09-20 收尾）

性能优化完成后，工程收敛到以下产线基线。核心决策：**PSRAM1 上 GPU/DPU 输出池 cover 关闭，
PSRAM0 解码 recon cover 保留**（后者是下行帧率提升的关键，且在无任务栈的 PSRAM0 上验证安全）。

### 13.1 最终配置一览

| 项 | 状态 | 说明 |
| --- | --- | --- |
| GPU 输出池 write-through cover（PSRAM1） | **关闭** | `doorbell_display_compositor.c` `comp_out_pool_init()` 不再调用 `comp_out_pool_cover_enable()`（helper 保留备查） |
| GPU 输出池所在芯片 | **PSRAM1 / CODED** | `COMP_OUT_HEAP = MEM_SLAB_HEAP_CODED`，分芯片带宽收益来自这里，与 cover 无关 |
| 解码 recon/DPB cover（PSRAM0） | **开启** | `CONFIG_H264D_RECON_WRITE_THROUGH=y`；PSRAM0/UNCODED 无任务栈，§12.24 验证安全；下行帧率关键 |
| GPU flexa 内部 ping-pong buffer | **HSRAM/SRAM** | `CONFIG_GPU_FLEXA_OUTPUT_BUFFER_ON_SRAM=y`，全程未改 |
| 任务栈 | **PSRAM1 / L2 cache 堆** | `CONFIG_TASK_STACK_IN_PSRAM=y` + `ram_regions.csv` 的 `AP_PSRAM_CACHE_HEAP`（attr5 L2 WB），对齐 `video_intercom` |
| 队列 | **PSRAM1** | `CONFIG_QUEUE_IN_PSRAM=y`（`queue.c` 原始 `psram_malloc`） |
| `ram_regions.csv` | 对齐 `video_intercom` | 含 `AP_PSRAM_CACHE_HEAP`，移除旧 `AP_PSRAM_NOCACHE_HEAP` |

### 13.2 为什么这样配（一句话依据）

- cover 是本 RCA 所有崩的必要触发条件（§12.24 干净 A/B：关 cover 稳、开 cover 崩）；关掉 PSRAM1
  上与任务栈同芯片的 cover 即根除，且 **36fps 不依赖该 cover**。
- PSRAM0 recon cover 在 §12.24（GPU cover 关 + recon cover 开）已长测稳定，保留其帧率收益。

### 13.3 已清理的调试/实验改动

- `DL_TEST_LOOP_GOP`：性能验证期间暂留 =1（GOP 回放测吞吐）；**出货前必须改回 0**（否则忽略真实
  网络、只循环缓存帧）。
- 已删除：`ap_main.c` 的 `pstress` 压测探针、`CONFIG_HEAP_UAF_AUDIT_POISON`、`doorbell_jsonrpc.c`
  的 cJSON SRAM hooks。
- 已还原 SDK 原始态：`freertos_smp_v2p0/.../queue.c`（`psram_malloc`）、
  `h264d/src/h264_decode_mem.c`（`MEM_SLAB_HEAP_UNCODED`）、
  `driver/psram/psram_test.c`（RAM 镜像 + DMA 回读 + 比对）。
- 保留 SDK write-through 基础实现（`psram_hal.c`、`h264_decode_mem.c` 的 recon cover 封装），
  因 PSRAM0 recon cover 仍启用需要之。

### 13.4 出货前检查清单

1. [ ] `DL_TEST_LOOP_GOP` 改回 `0`（`doorbell_downlink_video.c`）。
2. [ ] 确认无 `pstress` / `CONFIG_HEAP_UAF_AUDIT_POISON` 等调试项残留。
3. [ ] 正常模式（真实网络）长稳：多次拨号/挂断、断连重连、连续数小时。
4. [ ] 复核启动 log：无 `out pool cover: write-through`（PSRAM1 cover 确已关），有
   `dec cover: recon write-through ... area=0 psram0`（PSRAM0 recon cover 生效）。
