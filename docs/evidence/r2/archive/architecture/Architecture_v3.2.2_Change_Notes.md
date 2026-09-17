# Architecture v3.2.2 — 本轮局部修订与最终复核说明

**日期：2026-09-10**  
**对应正文：Architecture_v3.2.2_Implementation_Baseline.md**  
**直接基线：Architecture_v3.2.1_Implementation_Baseline.md**  
**评审输入：用户本轮粘贴的 v3.2.1 独立评审；不是历史 md(6) 评审。**

## 1. 批准边界与修订范围

接受本轮评审的结论：v3.2.1 主架构及 C1/C2/C3 已达到设计契约关闭条件，可以作为实施基线。不再重新讨论板卡、动态 DBM、K+2 池、FreeRTOS、四任务或研究方向。

v3.2.2 只落实两个口径澄清和三个代码接入提醒。正文保留 17 个主体章节，沿用 T01—T20、R0—R7；不增加新任务、定时器、软件库、存储子系统或 R8。

**本稿请求的是局部文档复核，不是替未提供的固件代码、时间预算或开发板结果签署通过。**字段名与唯一接线方式是根据评审落下来的实施选择；后续仍须与真实内核和目标代码集成验证。

| ID | 本轮要求 | 合并稿位置 | 既有验收入口 |
|---|---|---|---|
| L1 | 校准/诊断可无预测；独立验证须先有预测 | §5.2、11.3、12.1、13.1—13.2、14、附录 A | T13、R3/R6、协议 schema |
| L2 | S2 是截止事件，不强制制造 capacity-drop | §6.2—6.3、8、T18 | R5、模型同序事件 |
| I1 | 一个 IRQ 只发一次入口/一次出口 | §7.5、T12/T17 | R4、宏展开/调用点表 |
| I2 | 所有 FreeBufferQueue 来源都识别；STOP 后当前块可归还 | §4.2、4.5、T16 | R2/R3、真实队列适配 |
| I3 | 本次启动复位快照与实际故障归因 | §12.1—12.2、T19 | 启动代码与既有 watchdog 验收 |

## 2. 两处口径的唯一答案

### L1：运行机制与实验用途分开

保留原有 run_kind=PERFORMANCE / FAULT_STALL。增加实验用途和显式预测适用状态，属于协议/manifest 属性，不是 MCU 新状态。

| run_kind | experiment_purpose | prediction_status | prediction_id |
|---|---|---|---|
| PERFORMANCE | CALIBRATION | NOT_APPLICABLE | null |
| PERFORMANCE | DIAGNOSTIC | NOT_APPLICABLE | null |
| PERFORMANCE | VALIDATION | BOUND | 非空且对应事前保存的预测 |
| FAULT_STALL | FAULT_TEST | NOT_APPLICABLE | null |

字段必须显式出现；空字符串、缺字段、随意填的占位 ID 不等于 NOT_APPLICABLE。其他组合拒绝。校准不依赖已有冻结成本表即可开始；早期计量尚未通过风险门的结果标为 PROVISIONAL/UNQUALIFIED，不作为合格成本证据。

用途和绑定在 START 前固定，ACK/RunReport 回显；同一 request_id 改用途/配置/预测属于冲突。校准或诊断不能在看完结果后补预测，升级成独立验证。后续分析只能另标 post-hoc replay；真正验证必须开新 run。

主机负责预测文件及冻结校准已保存，固件负责字段/绑定/幂等。MCU 不检查 PC 文件系统，也不因收到 prediction_id 就自动证明科学独立性。汇总端再次核对 manifest 与原预测。ACK 到达可以晚于实际启动；不新增 ACK 接收屏障。

### L2：S0/S1 正常处理接纳，S2 只截止

S0 即使没有 FREE、发生 capacity-drop，也必须打开 CPU 窗口。S1 即使发生 capacity-drop，也必须关闭 CPU 窗口；S1 已在 primary cohort 外，属于 TAIL 普通输入。

S2 则不同：完整性检查通过后，关闭观察、设置停止闸门并请求任务级清理。**不再取 FREE、不重绑定、不入 READY、不增加普通 drop 或 admission-observed occupancy。**它保留原始硬件完成事件的序号/时间诊断，但不是一次普通接纳尝试。

模型执行相同 cutoff-only 规则。T18 改为“S0/S1 发生丢块，S2 在 FREE 为空时仍正确截止”，并增加 S2 有 FREE 的对照；两种 S2 都不得偷偷接纳或伪造 drop。

## 3. 三项代码接入约束

### I1：原子计量还需要正确的事件发射

本轮核对了固定 V11.1.0 的 portmacro.h 和 port.c：portYIELD_FROM_ISR 经退出宏已经发出一个 traceISR_EXIT 或 traceISR_EXIT_TO_SCHEDULER；SysTick handler 已有自己的 trace 对。

正文 §7.5 给出唯一接线表：普通外设最外层入口一次，尾部通过 portYIELD_FROM_ISR 一次；SysTick 使用 port 已有发射点；HAL callbacks 不另发一组；tick hook 是 SysTick 内部工作，不发独立 IRQ_ENTER/EXIT。任务选择由 task-switch hooks 接入，IRQ 请求调度不等于新任务已执行。

R4 必须有实际宏展开/调用点证据，并覆盖无需调度、唤醒高优先级任务、嵌套、SysTick/tick-hook 与错误返回路径。重复 EXIT 应被判为计量错误，不能靠加锁或静默忽略掩盖。

### I2：识别操作不等于只筛选 COMPLETE

FreeBufferQueue 的每次发送都检查来源。INIT 只填池；CANCEL 只取消归还；合法 COMPLETE 才在结果仍开放且属于 cohort 时生成一次正常结果；未声明/错误租约进入已有错误关闸路径。

关闭 processing_claim_allowed 只禁止认领下一块，不禁止当前合法块完成。观察关闭以后，当前块仍须归还，只是不再修改正式分类。基础设施故障与正常 STOP 不混为一类。

钩子没有 veto：发现错误后不能仅 return，仍按原协议先关接纳闸门。关闭普通诊断 trace 后功能钩子仍须生效。T16 同时覆盖正常 COMPLETE、INIT、CANCEL、未知来源、重复归还、STOP 后归还及 cutoff 后归还。

### I3：复位来源与测试归因分别证明

本轮核对 ST HAL 的 __HAL_RCC_CLEAR_RESET_FLAGS 确实通过 RMVF 清除复位标志。新版要求启动时先把原始 bitset 保存到本 boot 的不可变 BootEvidence，再清除。后续握手报告该快照，不重复读取清过的寄存器，不引用旧 boot 快照。

只有 IWDG 位还不足够：证据还要绑定同一测试，支持 Processing 实际进入 stall、Monitor 按无进展规则停喂，并排除已知基础设施终止/软件复位/人工中止。缺证或冲突为 INCONCLUSIVE/INVALID，不能 PASS。

诊断途径留给小型底层设计：可以是已被主机保存的有界诊断链，或经复位路径验证的固定容量记录。必须明确旧记录清理与失效；不引入 Flash 日志/文件系统，不假设普通 SRAM 必然跨复位保留。

T19 强制顺序为：真实 IWDG reset → 保存/清理新 boot 证据 → 再做软件 reset 反例。第二次不能因旧 IWDG 标志或旧测试记录被判为新成功；另测注入后基础设施错误以及缺证情形。

## 4. 不改变的预算与已通过契约

以下不变：最短必需 T_B=1.28 ms；nominal→rebind/drop ≤320 μs；nominal→ISR-end ≤448 μs；整个完成临界段初始目标 10 μs，180 MHz 下对应 1800 cycles。它们仍是待验证预算。

RuntimeEvent 嵌在完成钩子内的成本已属于完整临界段，预算不能漏计，模型不能双计。最终浮点 Benchmark 仍按 R7 回归 R2—R4，不能继承早期整数原型的性能证明。

QUIESCED ACK 的资源承诺、最后 ACK 后立即重启、固定 TAIL、不可变 RunMetrics、结果 TX 引用保护、条件 P99/删失规则全部保留。正文没有重新引入旧 t_finish，也没有缩小 mandatory operating envelope 来逃避失败。

## 5. 下一轮只需核对这些实施证据

| 实现包 | 必需材料 |
|---|---|
| TickServiceAdapter | 真实 FreeRTOSConfig/hook 接线；q0/release 附近短 scheduler suspension；service、release、skip、执行关系 |
| CompletionAdapter | 与真实 V11.1.0 队列成功路径集成；操作权限/旁路检查；完整临界段到达测试；关闭普通 trace 后功能仍在 |
| Clock64 / RuntimeEvent | 唯一事件发射表；真实嵌套及切换的双路径；窗口/回绕/异常记账；最长屏蔽片段 |

协议/schema 另提供用途矩阵的正反例和旧版本拒绝/迁移测试；T18/T19 采用本轮修正后的预期。PC mock 可证明局部状态规则，不替代真实内核钩子位置、ARM 屏蔽与 DMA 余量；随机上板长跑也不替代定点交错。

本次实际完成的是原稿读取、针对性官方源码核对、正文合并、逐处冲突检查，以及 Markdown 章节/表格/测试编号的文稿检查。**没有目标固件编译、运行或上板结果；没有把文稿检查当作 R0—R7 通过。**

## 6. 来源与文件追溯

本轮评审原文直接来自用户消息。官方定位来源与详细证据边界位于正文 [S3]/[S4]/[S6]/[S8]/[S9]：固定 V11.1.0 queue.c、port.c、tasks.c、portmacro.h；ST 官方 HAL RCC header。HAL master 是定位入口，不是构建版本锁。

附带 `Architecture_v3.2.1_to_v3.2.2.diff` 为机器生成的逐行差异，便于核对是否保留已批准条款。新正文是使用入口，差异文件只用于审查，不作为另一份独立规范。

**签署边界：C1—C3 的文档契约关闭状态保留；v3.2.2 请求局部复核。后续实现冻结和全范围硬件通过分别依据源代码、构建产物、定点测试及上板记录。**

### 文件 SHA-256

- `Architecture_v3.2.1_Implementation_Baseline.md`
  `439a429f604b8e38b00ed43fb1077193d5ad1351a9ea8114879a9ffdd0255371`
- `Architecture_v3.2.2_Implementation_Baseline.md`
  `5652bcdc12666a80e778d88a5a81acf271cdd4ddcac1c6c809d497a84885cee5`
- `Architecture_v3.2.1_to_v3.2.2.diff`
  `721f5a3ec966de859d1fe18525a46ef2dd1483eb64b6c6a2ae5bf109d8a71e22`
