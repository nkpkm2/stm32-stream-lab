# STM32 实时流式系统架构 v3.2.2
## 实施基线局部澄清合并稿：实验用途、S2 边界与代码接线

**日期：2026-09-10**  
**平台：NUCLEO-F446RE / STM32F446RE / FreeRTOS / C、受约束 C++ / Python**  
**状态：依据本轮评审，v3.2.1 主架构及 C1—C3 的设计契约已获文档层通过；本稿合并两处口径澄清与三项实施约束，供最终局部复核。适配器代码、时间预算和开发板工作范围仍须通过既定风险门。**

本稿以 `Architecture_v3.2.1_Implementation_Baseline.md` 为直接基线，只落实本轮评审 [R321]，不重新选择板卡、DMA DBM、K+2 池、四任务、计量方案或模型路线。主体章节仍为 1—17，T01—T20 与 R0—R7 保留并定点扩充，不增加主要子系统或验证阶段。

本版明确：校准/早期诊断可以显式无预测启动，独立验证必须事前绑定预测；S2 只执行观察截止，不再进入普通接纳/容量丢弃；IRQ 计量每次异常只发一组入口/出口事件；FreeBufferQueue 的所有写入都要识别来源；watchdog 使用本次启动的复位快照及可归因证据。已关闭的 C1—C3 不重新列为“待补设计定义”。

本稿是可独立交给评审与实现者的合并版本。与 v3.2.1 冲突时以本稿为准；未修改条款保留。附录 A 说明旧版本继承关系，§17 列出本轮闭环；不得从几个历史版本中各自选取不同语义。下文“必须”“目标”“通过条件”均为设计/验收要求，不是已测性能。

证据范围：[R321] 是用户本轮直接粘贴的 v3.2.1 评审；历史 [R]/[R32] 保留其原含义。本轮针对性核对 [S3]/[S4]/[S6]/[S8]/[S9] 的队列、IRQ trace、任务切换和复位标志路径；没有重新认证整板，没有编译、烧录或运行目标固件。在线源码核对不等于已归档完整源码 hash，实际构建版本/产物仍由 R0 提交。

---

## 1. 保留的主架构与必需工作范围

项目研究有限内存、固定优先级和计算需求如何影响实时数据处理，并用冻结的轻量模型预测未参与校准的配置。

```text
TIM6 → DAC → PA4 ──跳线── PA0 → ADC1 ← TIM2 TRGO
                                  ↓
                            DMA2 Stream 0
                                  ↓
                      两个 M0/M1 硬件地址槽
                                  ↓
                      K+2 个有效静态样本缓冲区
                                  ↓
                           ReadyQueue
                                  ↓
                          ProcessingTask
                        FIR / RMS / RFFT
                                  ↓
                       完成提交与缓冲区归还
                                  ↓
                         FreeBufferQueue

MonitorTask：健康、IWDG、诊断
CommunicationTask：命令与运行生命周期协调
InterferenceTask：受控计算干扰
Python：配置、事前预测、实验、结果存储与分析
```

| 项目 | 必需范围 |
|---|---|
| 采样率 | 20、50、100、200 kSamples/s；其他可实现值为额外配置 |
| 块长 N | 256、512；1024 为可选扩展 |
| 非 DMA 驻留容量 K | 1、2、4、8 |
| 最短必需块周期 | 256 / 200000 = 1.28 ms |
| 最大必需块事件频率 | 781.25 次/秒 |
| 干扰周期 | 2、5、10、20 ms |
| 高优先级干扰需求 | 校准后的 CPU 工作需求不超过周期的 80% |
| 必需信号 | 常量、单正弦、双音 |
| 应用任务 | Monitor、Communication、Interference、Processing，仍为四个 |
| Benchmark 通信 | 不传逐块 trace 或原始 ADC；只允许规定预算内的控制流量 |

上述范围是承诺验证的范围，而非已证明能无丢块运行的范围。“支持过载实验”允许 deadline failure 和 capacity drop，但不允许 DMA 所有权损坏或事件完整性失效。必需配置未通过采集安全门就是验收失败，不能静默改称“不支持”。

不增加自制 RTOS、lock-free 队列、bootloader、网络栈、多核、外部存储、复杂故障恢复或全 FreeRTOS 仿真器。

## 2. 平台实现基线与依赖边界

### 2.1 硬件资源

| 用途 | 固定资源 |
|---|---|
| ADC | ADC1 / PA0 / regular channel 0 |
| ADC 触发 | TIM2 TRGO |
| ADC DMA | DMA2 Stream 0 / Channel 0 |
| DAC | Channel 1 / PA4 |
| DAC 触发、DMA | TIM6；DMA1 Stream 5 / Channel 7 |
| PC UART | USART2，PA2/PA3，经 ST-LINK VCP |
| UART TX | DMA1 Stream 6 / Channel 4 |
| UART RX | RXNE 中断；不得未经变更评审改用与 DAC 冲突的 Stream 5 |
| RTOS tick | SysTick，1 kHz |
| HAL tick | TIM7，不得占用 TIM6 |
| 高分辨率时钟 | DWT CYCCNT，经统一 64 位扩展服务 |
| Watchdog | IWDG |

目标时钟为 SYSCLK/HCLK 180 MHz、APB1 45 MHz、APB1 timer 90 MHz、APB2 90 MHz、ADC 22.5 MHz。TIMPRE 等影响 timer clock 的设置必须固定。R0 同时核对供电、Power Scale 1、over-drive、Flash 等待周期和时钟源；不得仅凭 `SystemCoreClock` 通过验收。[S2]

ADC 采用 12-bit、单规则通道、外部上升沿触发、CONT=0、持续 DMA 请求/DDS、禁止逐样本 EOC IRQ。候选采样时间固定为 28 ADC cycles；22.5 MHz 下采样加 12-bit 转换约为 (28+12)/22.5 MHz = 1.78 μs，但触发延迟、输入建立与转换余量仍需 R1 检验。不能以 22.5 MHz 这个数字代替整个采样配置验证。[S1][S2]

ADC DMA 使用 peripheral-to-memory、半字宽度、memory increment、NDTR=N 个样本、direct mode、single burst、DBM；关闭不需要的 HT 中断，保留完成及相关错误检测。采样定时器不产生逐样本软件中断。

`AdcDbmDriver` 是 ADC/DMA 生命周期的唯一入口。HAL 可用于普通初始化，但不得先后叠加普通 `HAL_ADC_Start_DMA()` 和另一套 DBM 启动逻辑。驱动必须明确拥有寄存器配置、两个地址槽、IRQ、错误与中止路径；`HAL_DMAEx_ChangeMemory()` 的成功返回不构成 CT 安全证明。[S5]

### 2.2 工具与库

基线继续使用 Git、CMake、ARM GCC、STM32 烧录工具、STM32CubeMX 生成的受控初始化、FreeRTOS、CMSIS-Core/CMSIS-DSP，以及 Python 的 pyserial、NumPy、SciPy、pytest、绘图库。没有因为本轮修订而增加收费工具或硬件。

本稿的队列提交适配器以 **FreeRTOS-Kernel V11.1.0、GCC ARM_CM4F port** 为明确源码参照，不是声称这是最新版本。R0 记录实际完整 commit、文件 hash、CubeF4/HAL、CMSIS-DSP、编译器、浮点 ABI 和优化选项。更换内核版本必须重新核对第 4 节钩子位置。

固件版本锁由仓库管理；PC 依赖由 lock 文件管理。生成代码与自有代码分开，`.ioc` 入库。固件、native C/C++ tests、pytest、烧录与实验均提供命令行入口。本文尚不把未实际安装测试的软件组合称为“已验证环境”。

## 3. 容量、所有权与事务参与者

正确容量定义为：

\[
P=K+2,\qquad Q=READY+PROCESSING,\qquad 0\le Q\le K.
\]

在一个所有权事务完成、无暂存资源的观察点：

\[
F+Q=K.
\]

K 是上限，不等于当前 Q。十个最大缓冲区静态预留；每轮只有 K+2 个有效，其余 INACTIVE。FreeBufferQueue 与 ReadyQueue 的逻辑容量均为 K。

一个工作块从 ReadyQueue 取出后，即使还在检查 STOP 而未开始 DSP，仍由 Processing 持有并占据 Q。DMA ISR 取出 FREE token、解除旧槽位绑定、发布 READY 之间的短暂过渡由该 ISR 独占负责。此时不得由另一任务逐字段读取并断言稳定等式。

所有参与者必须遵守相同的短串行化域：队列、完成提交、块认领、停止闸门、cohort 截止。任务侧使用适合其操作的短临界协议；ISR 侧采用匹配的中断安全协议。应用 IRQ 均设置在允许 FreeRTOS API 的范围，DMA 使用其中最高紧迫度。更高紧迫度异常不得读写这套账目；故障异常令实验无效。

基线不允许未经审计的长关中断、长 scheduler suspension、ISR 浮点计算、printf 或 HAL 阻塞等待。精确 NVIC 表与 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 的关系作为 R0 输出，不能由各驱动分别猜测。[S4]

## 4. F1：唯一的 CompleteAndReleaseBlock 操作

### 4.1 不能继续使用的写法

```c
xQueueSend(free_queue, &id, 0);
t_finish = read_cycles();
/* 再修改共享 owner 或该 buffer 的元数据 */
```

队列操作自身安全，不代表返回后的时间戳也安全。V11.1.0 的成功发送路径在内部临界区更新队列，退出临界区后返回；两行之间确实可以发生抢占。[S3]

### 4.2 冻结接口，而非要求调用者自行拼接

`CompleteAndReleaseBlock` 是正常完成归还的唯一入口。输入是当前租约和 Processing 本地保存的完成元数据；返回一个只读的完成凭据。任何成功归还之后都不得再访问样本内存或属于该 buffer、可能已被复用的共享描述符。

正常 COMPLETE 只由 Processing 的普通任务上下文调用。基线禁止调用者用外层 PRIMASK/BASEPRI 临界区或 scheduler suspension 包住整个适配器；必须在调用入口而非已经进入内部队列钩子后检查这项前置条件。INIT、CANCEL 走经过审查的对应操作类型，不能复用 COMPLETE 标识。这个限制避免内部队列已经退出临界区、外层却仍在屏蔽 DMA 的隐含尾段。

本稿选定一个待 R2/R4 验证的具体适配方案：利用被固定内核版本的官方 `traceQUEUE_SEND` 扩展点。该钩子在成功分支、队列内部临界区内、实际复制 token 之前执行。它不是“复制已经结束”的钩子，必须保留这个区别。[S3]

执行关系为：

```text
Processing 准备本地 PendingCompletion
    ↓
零等待发送 buffer ID 到 FreeBufferQueue
    ↓
成功分支中的专用 traceQUEUE_SEND 适配器
    ├─ 校验当前操作、租约、run_id
    ├─ 读取逻辑提交时间 t_commit
    ├─ 提交一次且仅一次的完成结果与简短计数
    └─ 更新受保护的所有权/驻留账目
    ↓
内核完成 token 复制并退出临界区
    ↓
DMA ISR 此后可以取得 token
    ↓
调用者只做本地收尾，不再触碰已释放资源
```

钩子先识别是否为本项目 FreeBufferQueue；对该队列的**每一次发送**都必须校验显式操作来源，再分流为 INIT、CANCEL、COMPLETE 或非法操作。只有合法 COMPLETE 才可能产生正常完成结果，不能写成“不是 COMPLETE 就直接忽略”。其他队列不进入本项目归还账目。钩子不得调用 FreeRTOS API、DSP、同一队列或分配器；发送本身失败没有完成提交，意外满队列属于基础设施错误。具体权限与停止/截止后的结果处理见 §4.5。

### 4.3 完整不可抢占区间与时间含义

v3.2.1 已补齐临界段起点；本轮保留，不重新只描述 t_commit 后的尾段。[R32][R321][S3][S4]

```text
普通 Processing 代码，可被抢占
    ↓
t_lock：队列内部临界区实际开始屏蔽相关 IRQ
    ↓
队列可用空间检查、钩子前缀和租约校验
    ↓
t_commit：串行化的逻辑完成记录
    ↓
简短计数、FREE token 复制及内核收尾
    ↓
t_unlock：相关屏蔽实际解除，token 可被 DMA ISR 取得
    ↓
Processing 本地 epilogue，恢复普通任务优先级，可被抢占
```

对参与本协议的 DMA/RTOS IRQ，必须建模整个 `[t_lock,t_unlock)`，包括取时间戳之前的前缀。更高紧迫度故障异常不属于正常运行模型，出现即令实验无效。CPU 屏蔽中断仍不停止 DMA。

定义：

\[
C_{commit\_prefix}=t_{commit}-t_{lock},\quad
C_{commit\_suffix}=t_{unlock}-t_{commit}.
\]

`epsilon_commit` 保留为逻辑提交到资源可见的延后上界；`C_commit_prefix` 另外记录，不能因不包含于 epsilon_commit 而从模型消失。原先“整个相关短提交段不超过 10 μs”的初始工程目标现在明确指 `[t_lock,t_unlock)`；目标尚未取得实测证据。

`t_commit` 不是“内核已返回”或“所有 CPU 收尾已结束”。模型在 t_commit 记录结果，在 t_unlock 允许 pending DMA ISR 消费 FREE token；如果 IRQ 在临界段前缀期间到达，也必须等到 t_unlock。

R2/R4 可以用受控诊断构建、反汇编及定点中断注入建立 t_lock/t_unlock 的保守时间界限，不要求 Benchmark 为每块保存这两个额外时间戳。不得用可能已经被抢占很久的 xQueueSend 返回时间冒充实际解锁时刻。若改变外部屏蔽规则或内核版本，界限必须重新验证。

提交后的本地 epilogue 仍占用 Processing 的优先级，低优先级 Interference 不能同时运行；下一块不能提前开始。它不得访问已归还资源，其成本单独校准且不能算成 idle。根据 t_commit 更新的少量统计属于受保护尾段，不能递归要求逻辑时间戳包含全部以自身为输入的统计代码。

### 4.4 trace hook 不具有否决队列发送的能力

固定版本中，成功分支调用 traceQUEUE_SEND 后仍会继续复制 token。钩子检测到错误并普通 return，不等于 xQueueSend 失败。[S3]

本稿选定的失败契约为：租约、run_id、操作种类或 owner 校验失败时，在当前受保护区内锁存基础设施错误，关闭采集接纳与 Processing 新认领闸门，取消尚未生效的启动票据，不提交正常完成结果。DMA 入口必须先查故障/闸门，再取 FREE token 或重绑定；不得消费此后可能被内核复制进去的错误 token。

钩子本身只作有界状态锁存和必要的短寄存器关闸，不阻塞、不打印、不调用 FreeRTOS API、不进行完整 STOP。即使外层队列 API 返回 pdPASS，适配器也必须根据已锁存故障返回失败状态；本 run 标为 INVALID。无法确认所有权时执行已有完整复位路径，不尝试局部修改计数后继续测量。

### 4.5 操作分流、当前租约与构建约束

FreeBufferQueue 的发送必须由受控适配器声明操作；不能只按当前 run phase 猜测来源。操作上下文绑定调用者、buffer 租约、boot/run generation，防止遗留标记把一次非法发送误判为 COMPLETE。

| 操作 | 合法来源与资源前提 | 账目与结果效果 |
|---|---|---|
| INIT | 生命周期协调者；worker 已放弃旧资源、DMA 已停驻，正在重建当前有效池 | 只初始化 FREE token 与所有权；不产生完成、deadline 或 capacity-drop |
| CANCEL | Processing 的既有 STOP 清理路径；持有本 run 从 READY 取出的合法取消租约 | 归还并更新停止取消/资源账；不冒充 capacity-drop 或正常完成，不重写已关闭 cohort |
| COMPLETE，观察仍开放 | Processing 持有当前已经认领的有效租约，且无基础设施终止故障 | 按既有短提交协议归还；只有属于当前 cohort 的块才提交一次正式完成/截止分类 |
| COMPLETE，正常 QUIESCING 或观察已关闭 | 同一当前租约仍合法；STOP 允许当前块结束 | **关闭新认领不禁止旧块归还。**资源仍归还；观察关闭时仅更新允许的清理/诊断，不再改正式结果；早停 run 仍为 ABORTED |
| 未声明、错误调用者、旧 generation、重复归还、无合法租约或基础设施故障下冒充正常完成 | 不合法 | 进入 §4.4 的错误关闸路径；不提交正常结果，不把错误 token 留给正常 DMA 消费 |

因此不得用 `processing_claim_allowed == false` 或单独的 `phase != RUNNING` 拒绝一切归还。必须分别验证**能否认领新块、是否确实持有当前块、资源能否交还、正式结果是否仍允许写入**。有基础设施故障时，不通过伪造 COMPLETE 尝试修复正常账目；执行已有终止/复位政策。

INIT/CANCEL 使用对应的受控入口，COMPLETE 使用 CompleteAndReleaseBlock；未经声明的直接发送、FromISR 发送、overwrite 或其他旁路写入 FreeBufferQueue 均不属于基线允许的调用。静态调用点检查与测试必须覆盖这些禁止路径；不声称普通 traceQUEUE_SEND 能天然拦截所有不同 API。

专用钩子是功能适配器，不是可随普通诊断 trace 一起移除的观察点。仓库提供唯一宏入口、固定源码/构建绑定与 Benchmark 检查，防止 recorder 或空宏覆盖。关闭普通 trace 后仍须通过 T01/T16。钩子不是队列发送的 veto；非法路径仍按 §4.4 先关闸，不能只 return。归还成功后不再访问可能已被复用的 buffer 数据。

## 5. F2：统一等待、START 与 STOP

### 5.1 Processing 只在一个通道阻塞

ReadyQueue 仍保存真实描述符，但 Processing 只使用零等待 receive；无工作时统一阻塞在任务通知上。通知位为 `WORK | STOP | START`，持久存在，不要求一条通知对应一个 block。

ISR 按“先成功发布描述符、后置 WORK 位”的顺序操作。Processing 醒来后以队列为准逐块取，取下一块前检查停止闸门。队列空和准备等待之间到达的通知必须被保留，不得通过错误清零丢失。

STOP 使用同一个通知通道。这样不再依赖“通知能唤醒正在等待队列的任务”这个不成立的假设。[S6]

`TryClaimNextBlock` 规定认领的串行化点：若 STOP 先提交，该块只能取消；若 Processing 已先提交当前块认领，允许完成该当前块。取出但尚未确认认领的描述符仍需归还，不得消失。

### 5.2 START 准备与最终提交分开

Communication 是生命周期唯一协调者。只有上一 run 所有权回收完毕且两个工作任务均已停驻才允许 START。

准备阶段完成：配置验证、按 §13.1 的运行类别/实验用途检查及 boot/run/config/条件性 prediction 绑定、队列及已识别旧运行通知的整理、FIR state 与 previous sequence 复位、干扰 pending state 复位、统计复位、时钟及外设配置、TIM 装载与 UG、旧标志清理、ADC/DBM 武装、DAC 启动及稳定准备。通知整理必须早于发布新启动票据；worker 的旧执行尾部不得再清除新 START 通知。

此时 TIM2 仍停止。有效 RunContext 和 ISR 所需的接纳规则必须已建立。最后的启动提交按第 9 节在预定 tick 上完成；顺序是**先发布有效 phase/gates/epoch，再启动采样 timer**，不是 timer 启动以后补写状态。

中途失败执行逆向回滚：关闭接纳与释放闸门，停止已启动外设，确认 DMA EN=0，保持 worker 停驻。无法证明内存已脱离旧执行者时执行完整 MCU reset；不得以“初始化了一部分”状态返回可重配 IDLE。

### 5.3 STOP 唯一策略

策略不变：停止新采样，完成已经认领的当前 Processing 块，取消剩余 READY；Interference 不再产生新 job，并退出当前工作后停驻。

顺序冻结为：

1. 短停止提交：进入 QUIESCING，撤销尚未提交的启动票据，关闭 `acquisition_publish_allowed`、`processing_claim_allowed`、`interference_release_allowed`，停止 TIM2 新触发。
2. 在正常完成处理已经关闸后，关闭相关 DMA 中断源并请求 stream disable。等待 EN=0，不在长关中断区内忙等。
3. 此期间产生的 TC、pending IRQ、非零 NDTR 对应的部分块只记停止诊断；不得 seq++、正常发布 READY 或执行稳态 CT 交替断言。[S1]
4. 确认 ADC/DMA 不再访问样本内存，再清 DMA 标志、NVIC pending 与必要外设状态。两块原 DMA 绑定缓冲区进入独立 parked 账目。
5. 向 Processing、Interference 的统一等待通道发送 STOP。Processing 是 READY 取消的唯一执行者；Communication 不同时 drain ReadyQueue。
6. Interference 取消 pending job。正在运行的计算核在有界小分段边界检查停止请求，终止或完成后确认停驻。分段检查成本包含在校准中。
7. Communication 用可阻塞等待收集两个 worker 的 QUIESCED ACK。ACK 必须在本地测量收尾完成、所有指针交还后发出。
8. 核对 K 个非 DMA 缓冲区和两个 DMA parked 缓冲区，安全停止 DAC，封存报告，才进入 IDLE。

“全部 K+2 个缓冲区被核对”不等于把 K+2 个 ID 塞进容量 K 的 FreeBufferQueue。两块 DMA 缓冲区始终单独核对；所有 worker 停驻之后才能整体重建池。

STOP grace timeout 初始目标为 500 ms，需覆盖被支持配置的最大当前块处理时间并经 R3 验收。超时且指针持有状态未知时，基线采用完整 MCU reset，不采用仅重启外设后复用内存。早停的正式实验记录为 ABORTED，而不是正常结果。

### 5.4 QUIESCED 是资源承诺，不是“已处于 Blocked”

worker 在发送 QUIESCED ACK 前必须交还所有旧资源、完成旧本地统计收尾并清除自身旧 pending 状态。ACK 绑定 boot_id、run_id 和 worker_id；Communication 只接受本次停止事务对应的确认。

发送 ACK 可能立即唤醒更高优先级 Communication。允许 Communication 先完成 STOP、接收下一次 START，而 worker 之后才从发送 ACK 的 API 返回。ACK 后的旧路径只能进入保留通知的等待/重新检查循环，不能读写旧 RunContext、旧 owner、旧指标，也不能无条件清掉新 START/WORK 位。启动票据与当前 generation 是事实来源，通知只负责唤醒。

不以 eTaskGetState() 检查 worker “已经睡着”作为安全前提。判断依据是资源放弃承诺、对应 ACK 和核对结果。T13 必须覆盖最后一个 ACK 后立即处理新运行请求的交错。[R32]

## 6. F3：输入 cohort、观察截止与不可变结果

### 6.1 cohort 与 deadline

输入 block 从 0 编号，measurement cohort 是 `S0 <= seq < S1`。每个输入块，包括 capacity-dropped 块，都占有唯一序号和 nominal 时间。

\[
t_{nominal,k}=t_{epoch}+(k+1)NT_s,\quad D=NT_s.
\]

`t_epoch` 的来源和不确定度见第 9 节。按项目定义，完成采用 t_commit，等于 deadline 视为按时；基于物理时间精度作额外解释时，必须同时报告 epoch/commit uncertainty。接近误差区间的物理意义不能被过度宣称。

### 6.2 固定 TAIL 与三个边界的不同职责

本基线只使用预先配置的固定 tail_blocks，默认 8，最少 2。不在 cohort 全部完成时提前切换负载。TAIL 中采样、干扰、平台任务与优先级保持不变；新增输入不属于 primary cohort，但在 S2 之前继续参与真实接纳/丢弃。

令 `S2 = S1 + tail_blocks`。输入事件必须先通过事件完整性检查，再执行边界处理；不得把边界处理放进“成功 admitted”分支。本版固定如下唯一顺序：

| 输入事件 | 先执行的边界操作 | 此事件之后是否进入普通接纳 |
|---|---|---|
| S0 | 打开 CPU wall window；primary cohort 从本序号开始 | 是。有 FREE 则接纳；无 FREE 则计一次本 cohort 的 capacity-drop；两者都不影响窗口打开 |
| S1 | 关闭 CPU wall window；primary cohort 为半开区间，S1 不属于其中 | 是。作为 TAIL 输入接纳或 capacity-drop；不得加入 primary cohort 分母 |
| S2 | 关闭观察、设置停止闸门并请求任务级清理 | **否。S2 是截止边界事件，不再取得 FREE token、不重绑定、不发布 READY，也不制造普通 capacity-drop** |

S2 仍有真实硬件完成的序号、nominal/入口时间和完整性检查记录，可在原始硬件事件诊断中计一次；但它不属于 primary cohort，也不属于普通 admission-attempt/admitted/drop 或 admission-observed occupancy 的统计对象。原始硬件事件数与普通接纳尝试数必须分名保存，不能强求两者在截止处相等。

在 S2 输入事件的 DMA ISR 入口定义 observation cutoff。进入此 cutoff 前应满足：

\[
t_{obs} > \max_{k\in cohort}(t_{nominal,k}+D) + \epsilon_{time}.
\]

其中 epsilon_time 覆盖已声明的 epoch/commit 不确定度。配置阶段检查预期条件，运行时再核对。不成立时记录 INSUFFICIENT_OBSERVATION，不把未到期未完成块算作 deadline failure。事件完整性或真实硬件错误仍走 INVALID，不因它是边界而忽略。

### 6.3 完成与截止采用同一排序

CloseObservation 与完成钩子使用已有同一串行化规则。截止只读取已经提交的完成结果；未提交的至多 K 个 cohort 驻留块一次性标记为 EXPIRED_UNRESOLVED。完成临界段受保护时截止不得进入其中；截止先提交后，晚到完成只影响资源回收和允许的诊断，不能修改已关闭分类。

同周期时间戳相等时用事件序号确定先后，短 trace 必要时保留提交序列。模型使用同样顺序，不用容器遍历顺序决定分类。S2 在模型中也只关闭观察/停止，不附加普通 admission 或 drop。

S2 分支先完成有界分类关闭和闸门设置，再请求停止；它不等待 FREE，不依赖 ReadyQueue 入队成功，也不通过伪造 capacity-drop 来满足 T18。两块 DMA-owned buffer 按 §5.3 单独停放核对。后续 disable 导致的 TC/部分块仍按停止诊断处理。

截止所在 ISR 不执行等待 DMA EN=0、等待 worker ACK 或封存/发送报告等工作。其真实执行成本纳入已有全 ISR 及屏蔽预算；不因 S2 不走 rebind/drop 分支而漏测。停止引起的负载变化不进入已关闭的 cohort 结果。FAULT_STALL 保持 §12.1 的独立停止政策。

### 6.4 计数和分母

有效、正常完成观察的 run 满足：

\[
N_{input}=N_{drop}+N_{on-time}+N_{late-completed}+N_{expired-unresolved}.
\]

\[
N_{admitted}=N_{input}-N_{drop}.
\]

- DropRate = N_drop / N_input。
- DeadlineFailureRate_admitted = (N_late-completed + N_expired-unresolved) / N_admitted。
- OnTimeYield = N_on-time / N_input。

分母为零返回 N/A。基础设施错误、早停、观察不充分不进入上述正式比例的精度比较，但必须保存 run、配置与失败原因。

### 6.5 正式结果对象

将 `LiveDiagnostics`、可变资源账目和 `RunMetrics` 分开。

`RunMetrics` 依次经历 OPEN、OUTCOME_CLOSED、SEALED。CPU 窗口在其结束点关闭；cohort 结果在 observation cutoff 关闭；停止确认及完整性检查通过后 SEALED。后续任务切换、HAL tick、Monitor 只能更新独立 live counters，不能继续写已关闭字段。

GET_RESULT 只读取不可变封存对象。无须停止 SysTick 或 TIM7 才能读取它。STOP 失败可以让最终 RunReport 状态无效，但不得悄悄改写先前已关闭的 block 分类。

## 7. CPU 时间窗口与 DWT 并发规则

### 7.1 CPU 窗口不等于输入 cohort

为避免声称具备尚未建立的精确硬件边界，本基线将 CPU wall window 明确选为：

\[
[t_{irq}(S0),\ t_{irq}(S1)).
\]

这是两个事件完整性检查通过的输入块 S0/S1 的 DMA 入口串行化计量事件之间的区间，不是假装完全等于 nominal 的理想区间，也不以边界块是否被 admitted 为条件。t_irq 采用第 7.4 节 RuntimeEvent 在入口短事务内取得的时间戳；若另存更早的原始读取，只作诊断，不得事后回灌 runtime ledger。两个端点、实际长度及与 nominal 的偏移均上报，模型按同一定义截取。

窗口开始和结束都执行 `RuntimeCheckpoint`，它是第 7.4 节 RuntimeEvent 的一种操作，而非取时以后另行补做的无保护写入。DMA 入口对当前 run/gate、预计输入序号和窗口边界的识别须与该事件处于同一短同步动作；后续发现输入完整性失败则本 run 无效，不回填旧时间修饰结果。一个执行区间 [a,b) 对 CPU 窗口 [u,v) 的贡献为：

\[
\max(0,\min(b,v)-\max(a,u)).
\]

因此预热块在此窗口中实际使用的 CPU 要计入；cohort 块在 TAIL 才使用的 CPU 不计入此窗口。STOP 后读到的全程累计值不能直接除以 measurement 配置时长。

### 7.2 归属规则

CPU accounting 区分 task residency、已 instrument 的 IRQ/嵌套区间、idle thread、未归属平台开销。每个已划分区间只归属一次；中断时间不能同时计入被打断 task 和 IRQ。上下文保存恢复、exception entry/exit 等 C 层钩子不能精确覆盖的区间单列或给出误差界，不强行称为纯 CPU 工作。[S4]

总时间一致性要检查，但“总和正好等于窗口”本身不能证明归属正确。使用已知 synthetic work、嵌套 ISR 和 idle 被中断的测试验证归属。无法把 idle 与中断活动可靠分开的构建不得宣称提供精确 total busy utilization。

### 7.3 Clock64 只有一个实现

所有时钟扩展统一经 `Clock64_Now()`。内部保存 low32 与扩展计数，在极短的保存 PRIMASK、屏蔽、读取/更新、恢复原 PRIMASK 序列内操作；其中不调用内核 API。任务、ISR、钩子不得另行无锁修改 high word。

正常运行保证两次扩展服务间隔小于一个 DWT 回绕周期，并由 Monitor 定期服务。固定 180 MHz 时回绕约 23.86 s；调试暂停、时钟变化或无法保证回绕观测的运行无效。Clock64 不在每轮 START 清零，run 只创建新 epoch。

累计 cycles、latency sums 等按所需范围使用 uint64_t；位宽不是同步措施。RUNNING 的诊断读取采用短一致性读取，禁止长时间锁住整张直方图。Clock64 自身、runtime hooks 和所有短提交的屏蔽成本都进入 DBM 预算。

### 7.4 RuntimeEvent：取时间与记账共同提交

Clock64 单调不等于 runtime ledger 已安全。保留 v3.2.1 对下列分离写法的禁止：[R32][R321]

```c
now = Clock64_Now();       /* 内部保护已结束 */
RuntimeEnterIrq(id, now);  /* 可能已经落后于别的计量事件 */
```

基线选定一个有界 `RuntimeEvent(kind, context)`，所有任务切换、IRQ 进入/退出及窗口 checkpoint 都通过它执行；接口不接受调用者预先取得的时间戳。

概念顺序为（这是契约，不是已验证固件实现）：

```text
保存 PRIMASK，屏蔽可屏蔽中断
    ↓
通过 Clock64 内部受保护读取取得 now
    ↓
确认 now >= ledger.last_time；相等允许零长度区间
    ↓
结算前一区间，按窗口交集计入唯一归属
    ↓
更新当前 task/IRQ、嵌套栈与事件序号
    ↓
按操作执行窗口 OPEN/CLOSE；关闭字段以后不可再写
    ↓
恢复进入时的 PRIMASK
```

Clock64 的内部 `ReadLocked` 仅由已持有本协议保护的代码使用；对外 Clock64_Now 保留自身保存/恢复保护。不引入第二套扩展状态。`RuntimeEvent` 不改变已有 BASEPRI，不无条件开中断，不调用 kernel API、日志、分配器或可重入 trace。CMSIS 接口提供屏蔽与保存/恢复能力，但原子范围由本适配器自己保证。[S7]

一旦出现时间倒退、嵌套账目溢出或不匹配，在做无符号差值以前锁存计量错误并令 run 无效，不能把下溢当成巨大的正常执行时间。相同时钟值的事件用内部单调事件号排序。

Window close 与一般计量事件使用相同同步域。关闭正式窗口后可继续结算 LiveDiagnostics，但旧事件不得写回已经关闭的 CPU 结果。返回的时间戳可供其他只读记录使用，不能变成绕过接口的后续 ledger 修改依据。

PRIMASK 不屏蔽所有异常；NMI/HardFault 不参与正常账目写入，出现即令实验失效并走故障路径。C 层之前/之后的异常与切换开销继续使用未归属项或误差界，本修订不宣称消除了所有计量偏差。[S7]

整个 RuntimeEvent 的屏蔽成本计入平台/DBM 预算。其独立微基准、与完成钩子的嵌套使用、低优先级计量刚取时后高优先级 IRQ 变 pending、窗口关闭交错，均纳入 R4；不能仅测试 Clock64_Now 单调就宣布计量通过。

### 7.5 IRQ 与任务计量事件的唯一接线表

本轮新增的是实现接线约束，不修改 §7.4 的原子性方案。固定 V11.1.0 ARM_CM4F port 中，portYIELD_FROM_ISR 经 portEND_SWITCHING_ISR 调用 traceISR_EXIT 或 traceISR_EXIT_TO_SCHEDULER；xPortSysTickHandler 自身已有入口和出口 trace。[S4][S8] 因此同一真实 IRQ 不能同时使用手写 IRQ_EXIT 和该宏的隐含退出。

计量适配器必须提交一个实际宏展开/调用点表；基线采用以下单一接线方式：

| 路径 | 唯一入口发射位置 | 唯一出口/状态提交位置 |
|---|---|---|
| 项目普通外设 IRQ，包括 DMA、USART、HAL tick 等已 instrument IRQ | 最外层 handler 调用一次 traceISR_ENTER，映射到 RuntimeEvent(IRQ_ENTER) | 合流到一个尾部，仅调用一次 portYIELD_FROM_ISR(accumulated_woken)；由其 trace 出口映射到一次 RuntimeEvent。无需切换时传 pdFALSE，不能再手写 IRQ_EXIT |
| SysTick | 仅使用 xPortSysTickHandler 已有 traceISR_ENTER | 仅使用该 handler 已有的一个 traceISR_EXIT / traceISR_EXIT_TO_SCHEDULER 分支；外层 vector 不再包一组计量事件 |
| vApplicationTickHook | 它是 SysTick 内部工作，不发独立 IRQ_ENTER | 不发 IRQ_EXIT，也不调用带退出 trace 的 portYIELD_FROM_ISR。若需显式请求 PendSV，使用经过审查、不额外发退出事件的 port 路径；V11.1.0 的 portYIELD 仅负责挂起 PendSV [S8] |
| HAL callback / 子处理函数 | 不重复建立本次硬件 IRQ 的计量入口 | 只向最外层 handler 汇总唤醒请求，不自行调用带退出 trace 的尾部宏 |
| 任务切换 | traceTASK_SWITCHED_OUT 对应旧任务选择边界 | traceTASK_SWITCHED_IN 对应内核已经选择的任务；首次 scheduler 启动的 switched-in 单独初始化，不虚构先前 switched-out [S6] |

两个 IRQ 退出宏是同一类“退出当前 IRQ”的互斥路径；TO_SCHEDULER 仅另外表明请求调度，**不是新任务已经运行**。它不得自行发 TASK_SWITCHED_IN 或猜测下一任务。实际任务选择由已选 task hooks 接入，C 层无法覆盖的异常/切换尾部仍按 §7.2 保留平台开销或误差界，不伪造 PendSV/SVC 的入口出口来凑平总数。

所有可返回的外设 handler 路径（无事件、普通完成、错误、停止期间 pending）都必须到达一次共同尾部。由同一异常产生的多个 HAL callback 仍只形成一组入口/出口。真正的硬件 IRQ 嵌套则按实际 exception identity 配对，不能由 callback 层数代替。

RuntimeEvent 检查顶部 IRQ identity 与退出事件匹配；重复退出、缺失入口或错误嵌套令正式计量无效。不得静默吞掉多余 EXIT 让测试看似通过。预处理输出/调用图检查配合 R4 的“不要求调度”和“唤醒高优先级任务”两条真实内核路径；仅 mock 事件对不能证明接线正确。

功能性计量适配入口只有一处定义，第三方 recorder 不得再次发同类事件；普通诊断 trace 开关不改变本基线要求的接线。不新增 trace 系统，不修改调度器。

## 8. 百分位数与占用统计

基线 latency histogram 为 128 bins、范围 0—8D，另设 overflow。bin width 为 D/16；输出为区间或带分辨率标注的估计，而非精确分位点。

必须区分两个问题：

- histogram overflow：完成延迟超出直方图覆盖范围；
- unresolved censoring：最慢的一部分 admitted block 尚未完成，根本没有延迟样本。

默认输出字段为 `p99_completed_by_cutoff`，其统计对象是 cutoff 前完成的 cohort blocks。同时输出 N_admitted、N_completed、N_unresolved 和 overflow。

只要存在 unresolved，基线就不声称此值是全体 admitted 的 P99；全体字段返回 N/A/CENSORED。完成样本本身也有 overflow 时，用秩检查判断分位点是否可辨识：若目标秩落入 overflow，输出下界或 OUT_OF_RANGE，不能填成 8D。没有完成样本时返回 N/A。

arrival-observed occupancy 指普通 DMA admission 决策前观察到的 Q，不解释为时间加权占用；S2 仅作截止，不向该直方图再添加一条 admission 观察。模型经过相同 population 筛选、S2 截止和分箱后再比较。

## 9. tick 服务序号、启动与干扰释放

### 9.1 服务序号不是内核 tick

沿用已核对的 V11.1.0 行为：scheduler suspension 期间 tick hook 仍执行，但 xTickCount 暂不推进；恢复时补算 pending ticks 又避免重复执行 hook。xTaskGetTickCountFromISR 返回的是内核计数。故 hook 次数与该 API 的连续读值不可混用。[S6]

项目新增的只是一个已有 tick 适配器内部字段：

```text
tick_service_seq
```

它在每次真实进入 vApplicationTickHook 时推进一次；本 boot 内单调，不随 START/STOP 清零。基线用 uint64_t，只有 hook 写；Communication 读取和发布启动票据时，在短任务临界区内完成一致快照/票据提交，防止计数读取与票据发布跨越目标服务点。不得以 64-bit 或 volatile 自身代替同步。

q0、phase_ticks、period_ticks、next_release_seq 全部使用同一 service 序号域；不得用内核 tick 来比较其中任何一个字段。FreeRTOS 仍使用自己的 tick 处理任务延时与调度，不读取或修改内核私有 tick/pending 变量。

整个基线固件固定：

```c
#define configTICK_RATE_HZ       1000
#define configUSE_TICK_HOOK      1
#define configUSE_TICKLESS_IDLE  0
```

不允许应用在其他地方再次调用生产版本 tick hook，也不通过 tick stepping/tickless 补偿伪造服务次数。测试使用独立适配器测试入口，不将注入调用混入有效硬件实验。

### 9.2 释放状态与调度器暂停

每次 hook：推进服务号，核对服务完整性，处理该号的启动票据，然后处理该号的干扰释放。只做有界字段更新、时间记录、短 CommitStart 和合法 FromISR 通知；不执行干扰计算，不初始化 HAL，不做长循环。

当前服务号到达 planned release 时：若已有 PENDING/RUNNING job，记录 SKIPPED；否则建立一个 job 并通知 worker。job 直到实际完成提交才解除 occupied。输入记录使用 `planned_service_seq`、`actual_release_cycles` 和 run_id，避免含糊的 nominal_tick 命名。

scheduler suspension 可以推迟 worker 真正运行，但不能让已服务的启动/释放事件被跳过。内核补算 tick 不重复推进 service_seq、不再次释放任务。模型区分服务提交与实际 task dispatch；如果测试包含已声明的 scheduler suspension，还必须表达 worker 暂不能切换及恢复开销，而不是改完序号后假设 worker 已经执行。[S6]

必需共同测试向量保持：周期 2 ms，job0 在 0 ms 释放、5 ms 才运行、6.5 ms 完成；2/4/6 ms 被跳过，下一次为 8 ms。这里的时间表示未丢失周期服务下的服务/工作时间，不把内核积压补算变成新的真实 tick。

### 9.3 与采样的共同启动

Communication 在准备完成后，从受保护的服务号快照选择未来 q0（至少留出两个服务 tick 的准备提前量），并在同一短事务内发布有效启动票据。所有昂贵初始化都在此之前完成。

当 service_seq == q0，hook 校验 STARTING、run_id、票据和停止闸门；有效时先使 RunContext/WARMUP/接纳状态可见，紧邻 TIM2 CEN 写入前取 epoch，启动 TIM2，立即取之后的时间。DAC 已在准备阶段运行，不插入这个启动片段。phase_ticks=0 时，同一 hook 中先 CommitStart，再提交首个干扰 job。

STOP 在 STARTING 时撤销票据。若观察到 service_seq 已越过一个尚未提交的有效 q0，不追补启动，而记录 START_FAILED；合法的内核 tick 暂停不再通过使用错误计数域制造这种失败。

### 9.4 服务序号不能恢复丢失的硬件 tick

service_seq 证明 hook 被服务多少次，不证明硬件每个计数周期都得到服务。以 DWT 时间轴交叉核对服务间隔及累计相位残差；profile 必须声明可接受的服务延后界，并且该界需足够小以区分漏掉一个 tick 的情况。无法确认服务历史时，关闭释放/采集闸门并将 run 判为 INFRA_TICK_SERVICE，不补做一批 release 来美化序列。

软件启动偏移、tick 服务延后、时钟源与 CEN 总线同步误差在 R0/R4 建立保守界限，并进入冻结 calibration/profile。本轮硬件实际相位只用于符合性检查及事后解释，不替换事前预测输入。第一份 DMA ISR 仍不得冒充无延迟的真实硬件起点。

### 9.5 受保护的 tick 服务本身也占平台成本

所选 ARM_CM4F port 在 xPortSysTickHandler 中先屏蔽受内核临界区约束的 IRQ，再运行 xTaskIncrementTick，包括 hook。SysTick 的静态 NVIC 优先级较低，不等于 DMA 能在这一受保护区间抢占它。[S4]

模型包含这段有界不可抢占平台服务；tick hook 内的短 PRIMASK 片段属于该成本的一部分，不重复相加。允许的短 scheduler suspension 及其恢复片段也须纳入平台审查与采集余量验收。没有增加定时器、应用任务或自制调度器。

## 10. F5：DBM 全路径安全规则

ST 的 DBM 支持依据保留：只能更新当前非 active 的地址槽，写错可能触发传输错误并关闭 stream。[S1]

成功接纳路径至少记录：ISR 最早时间、admission 决策、MxAR 提交附近时间、ISR 出口。drop 路径没有 MxAR 写入，但必须记录 drop decision 和出口。

本版提出以下验收目标：

\[
L_{nominal\rightarrow rebind\ or\ drop-decision}\le0.25T_B,
\]

\[
L_{nominal\rightarrow ISR-end}\le0.35T_B.
\]

最短周期下分别为 320 μs 与 448 μs。这是含服务等待的工程余量目标，不是允许 ISR 主动执行几百微秒，更不是 WCET 证明。

写 MxAR 之前必须完成安全检查：run/gate 有效、错误标志无异常、CT 与预期事件一致、时间轴未显示可能漏事件，且“当前保守时间 + 最后检查至写入的审查预算”仍处于安全范围。预检查后的关键写入路径不得被未计入预算的更高紧迫度代码打断。

若已无法确认安全，禁止为了继续实验盲写地址；先关采集发布闸门、停止新触发并请求基础设施终止。事后读回/计时仅为附加验证，不能替代写之前的条件。[S5]

CPU 屏蔽中断不停止 DMA。时间预算必须包含 FreeRTOS 队列临界区、完成钩子、Clock64、cohort 截止、SysTick/TIM7 及全部获准控制流量。连续 capacity drop 也必须通过。

软件 seq++、CT 交替、NDTR/错误状态与 timer-derived 节奏交叉检查；任何无法区分一次与多次硬件完成的情况都判 INFRA_DMA_EVENT_LOSS，不凭连续软件序号继续发布可信数据。

必需范围失败不允许静默缩小。R7 加入实际浮点 DSP 后，对最终 Benchmark 构建重跑 R2—R4，不能继承早期纯整数固件的性能证据。[S4]

## 11. 模型与成本边界

### 11.1 admission 不是 nominal 到达

模型至少区分：

```text
nominal 最后采样触发
    → ADC/DMA 完成偏移
    → DMA IRQ 可执行
    → ISR admission 决策
    → READY 发布
    → Processing 调度与计算
    → t_lock：完整不可抢占提交段开始
    → 临界前缀
    → t_commit：逻辑完成
    → 临界后缀
    → t_unlock：FREE 对 ISR 可见
    → 可被抢占的 Processing epilogue
    → 下一块才可开始
```

可以使用校准的固定/有界偏移与小 CPU job，而非模拟总线每周期；但不能把 nominal 时刻直接当作 hardware admission 时刻，再宣称完全同义。

如果 FREE 在 nominal 到达后、ISR admission 前归还，模型应允许接纳。这是必须有的反例测试。

### 11.2 校准表是什么

成本表以完整 pipeline_id 为键，包含 N、taps、系数/窗口版本、features、数据类型、编译选项、库版本、DAC profile、instrumentation profile。32-tap+RFFT 必须有独立配置，不能混入笼统 Heavy。

分开记录 DSP/处理 CPU 工作、提交临界前缀/后缀、归还后的 Processing epilogue、FIR gap reset、admit/drop ISR 分支、Monitor/tick 等平台活动。不能把 interference OFF 下的整个墙钟跨度当成纯 CPU cost，同时再次加入其中已有的平台抢占。

CPU 归属尚未通过 R4 的表不能作为已认证 intrinsic service table。gap reset 成本在校准阶段获得，模型根据自己预测的输入缺口触发它，不能依赖验证后的真实缺口回填。

### 11.3 同时事件和相位

在 `[t_lock,t_unlock)` 内，IRQ admission 必须等待；SysTick 的内核屏蔽区也必须按第 9.5 节处理。未在这些段内的精确同刻事件按已声明的 IRQ/提交优先规则排序；timestamp ties 的策略及敏感性测试在验证前冻结。token 在 t_unlock 可用，不等于 Processing 的 epilogue 已结束；二者分别影响容量与 CPU 可用性。

对 experiment_purpose=VALIDATION 的运行，冻结成本表、phase uncertainty、admission offset 与模型代码后，先存 prediction artifact，再 START；其绑定条件见 §13.1。CALIBRATION/DIAGNOSTIC 可显式无预测启动，不能事后升格为 VALIDATION。任何该轮硬件实测工作量只能进入另行标记的 post-hoc replay，不替换原事前预测输入。

### 11.4 预测输入与安全压力输入分开

正式模型验证默认采用 QUIET 通信 profile：配置与预测绑定在 START 前完成，测量/TAIL 内主机只等待异步结束消息，不周期轮询 PING/STATUS。紧急 STOP 仍可用，但该轮被标为 ABORTED。

“每秒最多十个小请求”是第 13 节的采集安全压力上限，不是唯一通信时间表。需要研究控制流量时，manifest 额外声明请求类型、发送计划和主机时序不确定度；不能仅凭相同速率上限宣称两轮干扰相同。

Monitor、TIM7、DAC 等 profile 分别注明相位是受控、固定但未知，还是有界未控制；模型做相应敏感性或适用范围声明。DAC 提前启动不保证每轮第一 ADC 样本具有相同正弦相位。Timing stress 不要求逐样本重现；数字 golden vectors 可严格复现，模拟链路则对实际取得的同一批 ADC 样本做参考计算。[R32]

## 12. DSP、过载与 watchdog 的保留修正

FIR 对连续已接纳块保留状态；发现输入序号缺口先 reset state，并标记 transient。新 run 必须 reset；预热结束不能 reset。先用数字 golden vectors，再用真实 ADC 短快照与 Python 对同一输入对比。FIR 系数次序、去直流、RMS定义、FFT窗口、DC/Nyquist 打包处理与能量归一化写入 DSP 配置规范。

R0—R2 期间可以并行做小型 CMSIS 微基准来了解成本、ABI、Flash/SRAM，不必等 R7 才第一次编译 RFFT。完整 DSP 集成仍在后期。

不要求 fs×DSP 扫描必然跨过纯处理能力上限。若全部可持续，报告该结果；使用受控高优先级突发干扰展示合法的暂时容量不足。不得通过超规格采样、长关中断或关闭 watchdog 伪造过载。

Monitor 初始周期 10 ms。正常进展超时的初始目标为 250 ms、STOP grace 为 500 ms，均需用校准后的单块成本与最坏支持干扰验证。心跳条件只在确有应完成工作时建立，IDLE 不要求 DMA 进展，合法 deadline miss/capacity drop 不直接停喂。

`INJECT_STALL` 只使 Processing 真正停止并保留可检出的无进展状态；Monitor 根据同一进展规则决定停喂，不是看见命令就直接停喂。IWDG timeout 按 LSI 容差、Monitor 与进展窗口选择，记录实际区间与 debug 配置。COM 重新枚举不是目标 MCU reset 证据；用启动握手、reset cause、boot/run 绑定确认。

### 12.1 FaultTest.Stall：与正式预测验证分开的运行政策

运行机制仍只有 `run_kind = PERFORMANCE | FAULT_STALL`，不是新增 MCU 状态。PERFORMANCE 保留固定 WARMUP/MEASURE/TAIL 和 S2 自动停止；它可以用于 CALIBRATION、DIAGNOSTIC 或 VALIDATION，只有 VALIDATION 必须事前绑定预测，详见 §13.1。PERFORMANCE 中 INJECT_STALL 非法。FAULT_STALL 的用途固定为 FAULT_TEST，预测字段显式 NOT_APPLICABLE。

FAULT_STALL 的启动仍使用同一安全 START、DBM、四任务和健康规则，但不执行普通 cohort 截止自动 STOP。它只用于验证真实停工后的 IWDG 路径，不进入模型精度数据集。注入命令必须绑定 run/request 并幂等；它仅使 Processing 在确定测试点真实停止，不直接命令 Monitor 停喂。

Host 在测试前固定观察超时：至少覆盖最大合法无进展检测时间、按 LSI 容差得出的 IWDG 上界、启动/复位原因报告上界、传输余量。没有这些上界，不接受一个声称能验收 watchdog 的配置。观察期内不因普通命令响应超时发 STOP，也不运行普通 STOP grace 定时器抢先软件复位。

只有本次启动保存的原始复位快照支持 IWDG reset、新 boot 握手与被测 run/test 绑定正确，并且 §12.2 的实际停工/健康决策证据足以归因，才允许通过。观察超时、基础设施失败先复位、软件复位或人工 STOP 分别记录 TIMEOUT/INVALID/ABORTED；缺少可判定证据则为 INCONCLUSIVE，不算成功。失败后可明确人工/软件恢复，但不追溯改变结论。

安全错误处理始终有效；此 profile 不隐藏 DMA/内存错误，也不允许无界有效实验。测试结束后仍回到既有 BOOT/IDLE 生命周期。特别测试“在原普通 cutoff 附近注入”，证明被测 IWDG 路径没有被普通 STOP 抢先结束。[R32]

### 12.2 本次启动的复位快照与故障归因

启动代码只有一个复位证据采集入口。先保存原始 RCC reset-status bitset 到当前 boot 的不可变 BootEvidence，再调用所选 HAL/寄存器的 RMVF 清除操作。ST 官方 HAL 的 __HAL_RCC_CLEAR_RESET_FLAGS 通过设置 RMVF 清除复位标志；具体构建版本仍需锁定。[S9]

采集必须位于任何可能清标志的初始化路径之前；若 SystemInit/HAL 或生成代码更早执行清除，必须调整采集位置。保存位置不能随后被 C 运行时初始化覆盖。HELLO/GET_INFO 返回保存的本次快照，不能每次握手重新读取已经清除的实时寄存器，也不能引用上一 boot 的缓存。

按 bitset 保存原始证据，不假设所有复位位互斥。解码规则及清除顺序进入启动实现规格。每次新启动均采集并清理，包括正常上电、软件复位和 IWDG 后启动；不能等到第一次 watchdog 测试以后才开始管理历史标志。

IWDG 类型只说明复位来源，不单独证明由计划中的 Processing stall 引起。watchdog PASS 至少需要可相互核对的证据：

| 证据 | 必须能回答的问题 |
|---|---|
| boot/run/fault_test/request 绑定 | 复位前的注入及复位后的报告是否属于同一次测试，而非旧请求重放 |
| 实际停工记录 | Processing 是否真的进入指定 stall 点；仅命令 ACK 不等于已停止工作 |
| Monitor 决策及基础设施状态 | 停喂是否由规定的无进展条件触发，是否存在先行基础设施错误、STOP/软件复位或人工中止 |
| 当前 BootEvidence + 新握手 | 本次复位是否支持 IWDG 结论，而非沿用旧 IWDG 标志；证据冲突时不判 PASS |

下层可以选择主机已收到并保存的有界诊断链，或一种经过相应复位路径验证的固定容量诊断记录；必须说明记录提交、读取、失效与清理规则。不强制增加 Flash 日志、文件系统或新持久化子系统，不未经验证假设普通 SRAM 状态一定跨复位有效。

在故障检测到 IWDG 复位的间隔中出现新的已知基础设施错误，不能继续沿用早先的“计划 stall”证据判成功。所选证据途径必须能区分此情形；证据丢失、过期、不完整或互相冲突则 INCONCLUSIVE/INVALID，保存原始资料，不以“没有收到错误消息”替代归因证明。

T19 必须先执行一次真实 IWDG reset，再执行软件复位反例；第二次不得因历史 IWDG 标志或旧诊断记录被判为新 watchdog 成功。实际保存手段及其反例测试是小型实现交付，不重开本架构的故障管理设计。

## 13. 通信与结果绑定

采用有界二进制帧。候选线规为 Magic、版本、类型、请求 ID、长度、payload、CRC-32；最大 payload 256 B。CRC参数、字节序和测试向量写入 protocol.md，C/Python 必须共享 golden frames。

RX 有长度检查、丢字节/溢出计数、有限重同步和处理预算；错误帧不能改变运行状态。USART 8N1、115200 为 bring-up 起点，Benchmark 不以串口速度为实验变量。正式运行只允许 PING/小 STATUS/STOP；GET_RESULT 在封存后分块取回。

控制流量安全验收 profile：每秒最多 10 个小控制请求、每个运行期请求 payload 不超过 64 B；覆盖许可范围内的不利突发，不只测均匀间隔。超预算输入拒绝/限流并记录，不以无限高优先级解析压垮采集。正式模型验证默认采用第 11.4 节 QUIET profile；FAULT_STALL 另外允许一次有效的 INJECT_STALL，不与 PERFORMANCE 命令表混用。

START 绑定 boot/session、request_id、resolved_config_hash、run_kind、experiment_purpose 与显式 prediction binding；具体允许组合见 §13.1。ACK 丢失后完全相同的 START 重试返回同一 run，不再开始一轮；同一 request_id 携带不同用途、配置或预测则拒绝 REQUEST_CONFLICT，不覆盖原绑定。STOP 重试返回停止进度或同一最终结果；只有收到双方 worker ACK 和 DMA EN=0 后才返回 STOPPED。目标 reset 后旧 boot 请求不能自动启动新 run。

UART TX DMA 使用专用、固定大小 staging buffer。DMA 尚未确认停止访问前，CPU 不得修改或复用其中内容，也不能让下一 START 的统计复位覆盖它。每个结果片段绑定 boot_id、run_id、result_id、片段号及总长度。

本版选择有界保留策略：前一封存结果仍有传输/下载引用时，不允许复用其存储；新 START 可返回 RESULT_BUSY。只有主机已明确消费或放弃旧结果、相关 TX DMA 已结束，才允许复用。无需为任意多历史 run 保留 MCU 内存；长期存储仍在 PC。普通短状态帧也遵守同一 TX 所有权规则。

无效和 aborted runs 同样存盘，正式模型误差汇总显式排除并列出数量与原因。

### 13.1 运行机制与实验用途分开

本轮按 [R321] 补齐以下协议/schema 规则；它们不是新 MCU 状态，也不增加 run_kind。

| run_kind | experiment_purpose | prediction_status / prediction_id | 用途及结果资格 |
|---|---|---|---|
| PERFORMANCE | CALIBRATION | NOT_APPLICABLE / null，两个字段必须显式出现 | 可以产生第一批或更新的校准数据；不要求先有冻结成本表，不进入独立预测精度汇总 |
| PERFORMANCE | DIAGNOSTIC | NOT_APPLICABLE / null | 早期 bring-up、微基准和性能诊断；可做另标 post-hoc 分析，不计独立验证 |
| PERFORMANCE | VALIDATION | BOUND / 非空有效 ID | 必须在 START 前保存预测并核对完整绑定；仅此用途有资格在其他门通过后进入独立验证汇总 |
| FAULT_STALL | FAULT_TEST | NOT_APPLICABLE / null | 仅按 §12.1—12.2 检验 stall/IWDG；不进入预测精度数据集 |

其他组合一律拒绝。NOT_APPLICABLE 是显式状态，不是缺字段、空字符串或一个貌似有效的占位 prediction_id；字段缺失、冲突或未知值按 schema 错误拒绝，不静默猜用途。二进制编码可以用 presence/status tag 表达 null，但 C/Python golden frames 必须一致。

CALIBRATION 不需要已冻结成本表才能启动，也不能因没有预测而绕过硬件/所有权安全门。早期指标或 CPU 归属还没通过 R4 时，输出带相应 PROVISIONAL/UNQUALIFIED 证据状态；这些结果不能被导出成已验收的 intrinsic-cost table。后续按已有风险门选择合格校准数据并冻结，不反过来让有效预测成为获得第一批数据的前置条件。

实验用途、prediction binding、resolved configuration 与 schema version 在 START 准备阶段绑定到 RunContext 和主机 manifest，ACK 与最终 RunReport 原样回显；整个 run 内不可改。校准或诊断运行结束后补一个预测文件，不能升级为 VALIDATION；只能创建另标 post-hoc replay，独立验证须重新发起新 run。

### 13.2 独立验证绑定：主机和固件各自负责什么

VALIDATION 的顺序固定为：

```text
解析并确认 effective/resolved configuration
    → 选择已冻结 calibration/model/profile
    → 生成 prediction artifact
    → 成功持久保存 artifact、hash 及 manifest
    → 以 BOUND 状态发送 START
    → 设备按既有票据启动；主机接收并核对 ACK/run_id
    → 收集封存硬件结果
    → 按原始 binding 比较
```

ACK 经串口到达主机的时间可以晚于实际采样启动；这里不新增“等待主机收到 ACK 才允许运行”的协议。ACK 丢失按既有幂等 START 重试，不能重新生成另一份预测或启动另一轮。

预测至少绑定 resolved_config_hash、模型版本/哈希、校准表版本/哈希、instrumentation/platform profile、相位/窗口定义及结果 schema。START 的 prediction_id 指向这个已保存对象，而不是运行结束后才出现的临时名字。

主机负责验证文件已成功保存、模型/校准已冻结且适用，并在发送 START 前固定顺序证据；固件负责字段组合、启动绑定、幂等与回显一致。**MCU 不假装能够检查 PC 文件系统，也不把收到一个字符串视为科学独立性的证明。**数据汇总端再次核对预测文件及完整 manifest；缺少任一必需证据，不进入 independent-validation 汇总。

该轮硬件测得的成本/相位可用于事后解释，但不得改写预测输入。发现绑定错配、未知 schema 或历史 run 的预测被误用，记录错误，不自动降成 DIAGNOSTIC 并保留“验证成功”称号。

新增条件字段和 S2 截止口径要求显式 schema 修订。下层 protocol.md / experiment-schema 固定具体版本与编码；历史文件只能显式迁移或拒绝，不能按缺省值偷偷把旧 PERFORMANCE 全归为 VALIDATION。具体字节布局是实施规格，不需要增加新通信命令。

## 14. 预先固定的实验与模型评价

本节的“事前预测/独立验证”仅适用于 §13.1 的 VALIDATION。CALIBRATION 与 DIAGNOSTIC 可以使用同一安全实验生命周期和统计格式，但用途标签及数据集资格不变。

A：扫描 fs×完整 pipeline，分别报告 timing failure 和 lossless-admission 边界；边界可不在扫描范围内。  
B：高低优先级、短频繁/长突发干扰；同时报告 work_units、实际执行量、skipped releases 和相位。  
C：K=1/2/4/8 的 drop、conditional latency、unresolved、on-time yield。  
D：冻结模型对未见的 fs/K/interference 组合进行事前预测。

系统正确性门与模型精度评价分开。所有权、事件完整性、启停与统计正确性必须通过；不强制模型每个配置都“误差很小”。

正式矩阵在 validation 前存入 manifest：每个配置至少 5 次重复，固定一组事前声明的 phase_ticks；需要哪些相位由实验问题决定，不能结果出来以后择优。比例用绝对百分点误差，零 drop 不计算无意义的相对百分误差；百分位先统一 population、分箱与删失规则。边界按离散配置格上首次满足声明条件的区间比较，不把有限运行的零观测冒充永久零风险。

## 15. 静态资源、errata 与交付文件

最大原始样本池约 20 KiB。四任务栈、MSP、FreeRTOS 对象、DSP工作区、FIR状态、UART缓冲、运行账目及直方图统一列入 linker map 预算，同时检查 Flash。运行阶段禁止 malloc/free/new/delete。受约束 C++ 禁止异常和 RTTI，不引入隐藏动态容器。

库版本/优化/浮点 ABI 的改变使相关校准失效。R0 保存 board revision、芯片 revision、时钟来源、原始参考资料版本和 hash。

ERRATA_APPLICABILITY.md 至少覆盖目标芯片适用的 FPU/异常、RCC使能延迟、ADC、DAC DMA启停、TRGO、DMA、USART与IWDG/debug。不能把“ISR 不使用浮点”当成所有 CPU errata 均已规避。具体 workaround 以被存档的官方 ES0298 适用条目为准，必须对应代码和测试；本文不重复未经本轮逐条核验的操作细节。

必要交付文件保留：architecture.md、hardware-map.md、buffer-contract.md、run-lifecycle.md、timing-and-statistics.md、protocol.md、SETUP.md、ERRATA_APPLICABILITY.md、dependency-locks、calibration/validation manifests、测试与原始结果。此次澄清分别落入这些既有实施规格：§13.1—13.2 的用途/预测条件表和 schema 规则、§6 的 S2 事件账目、§7.5 的 IRQ 唯一发射表、§4.5 的操作权限表、§12.2 的启动证据规则。不得只在评审摘要里出现而正文/测试保持旧口径。

## 16. 定点反例与风险门

| 编号 | 必须覆盖的案例 | 验收含义 |
|---|---|---|
| T01 | FREE 发布边界请求高优先级抢占及 DMA 完成 | 不出现无界提交间隙；旧任务不写新 owner 字段 |
| T02 | 同一时刻完成提交与 cutoff 竞争 | 只能在 cutoff 一侧计数，不能 complete+unresolved 双计 |
| T03 | ReadyQueue 空、Processing 等待时 STOP | 统一通知唤醒并 ACK，不永久等待队列 |
| T04 | DMA 仅填半块时 STOP、存在 pending TC | 停止 TC 不算完整块，下一轮不收到旧完成事件 |
| T05 | Interference pending/running 时 STOP | 无遗留 job、无旧相位，两个 worker 均停驻 |
| T06 | DAC 已启动但 ADC 武装失败；预定 q0 前 STOP | 回滚到真实停驻态或完整 reset；旧启动票据不会复活 |
| T07 | 观察在 deadline 前结束 | 拒绝正式失败率，不把尚未到期块判成失败 |
| T08 | 工作跨 CPU window 两端及 TAIL | 按实际时间交集计入，不按 cohort 全部归入 |
| T09 | 0/5/6.5 ms 的延迟干扰 job | 2/4/6 ms skipped，8 ms 才有下一 job |
| T10 | FREE 在 nominal 后、admission 前归还 | 轻量模型与硬件语义均允许接纳 |
| T11 | K 全覆盖、成功与连续 drop、最坏允许控制流量 | 两条服务时限与整段 ISR 时限均通过 |
| T12 | >60 s、嵌套 IRQ、DWT 回绕；外设 IRQ 的不切换/唤醒高优先级任务两条出口；SysTick 带 tick hook | Clock64 单调；每次 IRQ 恰好一次入口/出口；TO_SCHEDULER 不提前切换任务归属；窗口关闭后不再写入 |
| T13 | START ACK 丢失、重复 START/STOP、目标 reset；四种用途及预测字段缺失/冲突/错配 | 校准显式无预测可启动；无绑定验证拒绝；用途不可事后升级；重试不换绑定、不重复启动、不串 run |
| T14 | deadline 附近与 histogram overflow/unresolved | 标注精度、分母与population，不伪造 P99 |
| T15 | 短 scheduler suspension 分别跨越 q0、release；resume 补算 | service_seq 一次一进；启动不漏、释放不漏不重；DWT 服务异常不能补跑 |
| T16 | DMA 在完整提交段周围到达；INIT/CANCEL/COMPLETE/非法来源；claim 已关、cutoff 后归还、重复租约 | 所有 FreeBufferQueue 来源均检查；合法当前块可归还；关闭分类不再写；非法 token 先关闸，不通过忽略来源蒙混 |
| T17 | 低优先级计量短事务内使高 IRQ pending；窗口关闭；错误注入重复 IRQ_EXIT | 正确事件原子且配对；重复出口必须报错，不能以加锁或静默忽略掩盖错误接线 |
| T18 | S0/S1 FREE 为空并 capacity-drop；S2 FREE 为空触发截止，另测 S2 FREE 非空 | S0/S1 窗口照常开/关；S2 两种情况下都只 cutoff，不取 FREE、不发布、不增加普通 drop 或 admission 占用样本 |
| T19 | 原普通截止附近 stall；真实 IWDG 后紧接软件复位；旧/缺失诊断；注入后基础设施错误 | 本 boot 快照先存后清；本测试实际停工与健康证据可归因才 PASS；旧标志、IWDG 类型单项、缺证或普通复位不能冒充 |
| T20 | 结果 TX 尚未结束时重复 GET_RESULT / START / 释放旧结果 | 在途字节不可变、结果不串 run；有界 RESULT_BUSY 与释放规则有效 |

R0：时钟、电源、采样设置、中断全集、工具链与资料版本。  
R1：采集节奏、ADC/DBM小驱动、部分块停止及中止 TC。  
R2：K+2池、完成钩子、接纳/drop、最坏获准负载与服务余量。  
R3：完整启停、空等待、pending工作、失败回滚及定点交错；然后重复启停至少1000次。  
R4：Clock64、runtime窗口、逻辑提交误差、相位、嵌套与cutoff一致性。  
R5：合成已知结果贯通 warmup/cohort/tail/封存，守恒与分类同时核对。  
R6：成本边界、延迟admission、skip机制和prediction-before-run验证。  
R7：DSP数字与模拟路径验证，随后最终Benchmark固件回归 R2—R4。

全部案例沿用现有关口：T15→R0/R4/R6，T16→R2/R6，T17→R4，T18→R5，T19→既有 watchdog 验收，T20→R3/通信验收。本轮把 T12 的真实 IRQ 接线检查加入 R4，把 T13 的用途/预测条件矩阵同时放入 R3/R6；不增加 R8。T13 继续覆盖最后一个 QUIESCED ACK 后立即重启：结果存储解除引用后必须真正接受新 START，不能靠持续 RESULT_BUSY 避开交错。普通 trace 关闭的最终 Benchmark 构建也执行 T01/T16。

随机长跑不能替代定点测试，PC mock 也不能单独证明实际内核扩展点、ARM 屏蔽或 DBM 时间余量。下轮必须用真实 V11.1.0 队列/port、简单工作量和三个适配器形成最小集成；提交预处理宏展开/调用点表、源码及构建标识、指定交错记录和上板时限记录。初期 R0—R2、host 协议/配置、数字 DSP reference 可并行；正式 CPU 归属、精确容量/相位比较和批量独立验证仍受相应门约束。

10 μs 完成临界段目标（180 MHz 下为 1800 cycles）仍为待验证预算。RuntimeEvent 若嵌在该临界段内，其开销包含在整段实测值中，不在模型里再次重复加算；SysTick、队列和截止等实际连续/嵌套屏蔽路径也要测，不能将几个短函数单测值当成整机保证。目标未过先检查局部代码、测量边界与反汇编，不静默修改目标；最终浮点 Benchmark 仍按 R7 回归 R2—R4。

## 17. 本轮评审闭环、实施边界与版本索引

### 17.1 C1—C3 按文档契约关闭，不重开主架构

本轮评审 [R321] 已批准 v3.2.1 作为实施基线，并明确关闭下列“缺失设计规则”的状态。本稿保留其实现方案，只补代码接线和证据要求。

| 已关闭项 | 本版保留的规则 | 后续证据，不是新架构问题 |
|---|---|---|
| C1 | 独立 tick_service_seq；q0/release 单域；tickless 关闭；DWT 服务完整性检查 | 实际 hook 接线、暂停跨 q0/release 记录及相位预算 |
| C2 | 完整 t_lock→t_commit→t_unlock；无未经审查外层屏蔽；非法发送先关闸；epilogue 成本保留 | 真实 queue.c 集成、操作分流、定点 DMA 到达及完整临界段测量 |
| C3 | RuntimeEvent 在同一短保护段取时、结算、修改归属/窗口，不接受旧时间戳回灌 | 唯一入口/出口发射表、嵌套与实际任务切换、窗口关闭及屏蔽上限 |

### 17.2 本轮只关闭两个口径缺口、落实三个实施提醒

| 本轮事项 | 修订位置 | 验收入口 |
|---|---|---|
| L1：校准不能被有效预测前提锁死 | §5.2/11.3/12.1/13.1—13.2/14：运行机制与用途分离，校准/诊断显式 NOT_APPLICABLE，验证严格 BOUND | T13、R3/R6；实际协议/schema 条件表 |
| L2：S2 不是必须产生 capacity-drop 的普通输入 | §6.2—6.3/8：S0/S1 即使丢块也处理边界；S2 仅 cutoff，普通接纳账目不含它 | T18、R5、模型同序事件测试 |
| I1：同一 IRQ 不得发两个退出 | §7.5：port/应用/SysTick/tick hook/callback 各自唯一发射位置；请求调度不等于任务已切换 | T12/T17、R4、预处理/调用点表 |
| I2：操作识别不能只过滤 COMPLETE；停止后当前块可归还 | §4.2/4.5：全部来源验证、权限分流、claim/lease/结果开放独立检查 | T01/T16、R2/R3；关闭普通 trace 的构建 |
| I3：复位类型与本次试验因果证据分开 | §12.1—12.2：本 boot 原始快照先存后清；停工/健康/错误证据匹配；缺证不 PASS | T19、既有启动/watchdog 验收 |

QUIESCED ACK、固定 TAIL、不可变结果、TX 引用保留、cohort 和 conditional percentile 不再重新列为待设计。主架构继续按基线推进；三个适配器的实现冻结取决于代码/构建/指定测试，工作范围通过取决于开发板实测，不混为同一种“通过”。

下一轮优先交付三个小而真实的实现包：TickServiceAdapter、CompletionAdapter、Clock64/RuntimeEvent。每包含固定内核/port、必要宏与调用图、状态/反例测试、编译产物标识和待补的上板证据。此稿本身没有提供这些固件验证结果。

本版新增字段名、错误分类与唯一接线方式是落实评审的局部设计选择，不声称评审员已批准尚未看到的代码。除实测证据显示某项必需工作范围与安全不变量不可同时满足外，不重新打开板卡、RTOS、缓冲拓扑或研究问题。

### 附录 A — 保留、替换、下沉索引

| v3.1 / v3.2 内容 | 本版地位与位置 |
|---|---|
| 板卡、时钟、TIM2/TIM6/TIM7、ADC/DAC/DMA/VCP 映射 | 保留，§1—2；最终数值和 errata 实施证据在 R0/R1 |
| K 最大容量与 Q 当前驻留量 | 保留 v3.2 更正，§3；P=K+2、0≤Q≤K，稳定提交点 F+Q=K |
| 四任务相对优先级 | 保留：Monitor > Communication > Interference HIGH > Processing > Interference LOW > Idle；Interference 是同一任务的两种配置，数字 NVIC/任务优先级由 R0 表冻结 |
| DSP 配置清单 | 保留：Light=mean/RMS/peak；Medium=32或64 taps FIR+RMS/features；Heavy=64 taps FIR+RFFT/features；32-tap FIR+RFFT 是独立 baseline candidate。N=256/512 核心，1024 可选，FFT=N；完整配置 ID 不得混合校准 |
| FIR 缺口状态与数字/模拟验证 | 保留，§12；具体系数、窗口、归一化和容差下沉到 DSP specification，不得默认为任意值 |
| 旧 t_finish | 不再作为独立、无歧义字段使用。正式逻辑完成用 t_commit；FREE 可见由 t_unlock 限定；收尾用 epilogue。旧名输入拒绝或通过显式 schema migration，禁止静默重命名 |
| t_nominal、t_ready、t_start | 保留物理/软件区别；t_ready 为 DMA 入口的已串行化软件时间，R_nom=t_commit−t_nominal、R_sw=t_commit−t_ready；不是绝对精确物理完成时刻 |
| 初始化/预热/测量/尾段/停止 | 保留，§5—6；FaultTest.Stall 只改变故障试验的停止触发政策，不改内存安全协议 |
| effective configuration | 保留：requested/effective fs、timer/core clock、PSC/ARR、N、K、pipeline ID、work_units、period/phase 与 DAC/instrumentation profile；实际整数/有理数编码下沉到 config/protocol schema |
| 结果公共字段 | 保留 boot/run/config/schema、输入/接纳/drop/完成/过期未完成、分母、条件直方图、CPU窗口及完整性；新增明确 run_kind/experiment_purpose/prediction_status 与可空 prediction_id，§13.1。S2 只属截止/原始事件诊断，不混入普通接纳计数 |
| 预测先于验证、冻结校准、模型允许误差 | 保留，§11/13.1—13.2/14；适用于 VALIDATION。CALIBRATION/DIAGNOSTIC 显式无预测，不能事后升格；replay 单列 |
| 依赖、静态内存、errata、协议字节格式 | §2/13/15 保留约束，具体实现清单下沉到 SETUP/protocol/ERRATA_APPLICABILITY/版本锁；未写出的细节不得由不同开发者各自定义 |
| T01—T20、R0—R7 | 保留既有编号；本轮修正 T18，扩展 T12/T13/T16/T17/T19，其余继续有效；没有新增验证阶段 |

本版不恢复已替换的 t_finish、含糊内核 tick 或停止顺序。v3.2.1 中“所有 PERFORMANCE 必须预测”和“S2 必须 capacity-drop”的可歧义措辞，以本版 §13.1 与 §6.2 唯一规则替换。遇到未消除的实质冲突，只暂停对应局部接口定版，不任意选取旧版本。

---

## 参考资料与证据边界

[R] 历史 v3.1 评审，用户文件“粘贴的 markdown (1)。md(5)”；沿用 F1—F5 的闭环，不作为本轮新评审。

[R32] 历史 v3.2 评审，用户文件“粘贴的 markdown (1)。md(6)”；v3.2.1 补齐 C1—C3 的依据。

[R321] 用户本轮直接粘贴的 v3.2.1 独立评审：C1/C2/C3 契约关闭；校准预测字段与 S2 测试需局部澄清；IRQ 唯一发射、完成操作分流和复位证据进入实施审查。没有为这段粘贴内容虚构上传文件名或实测报告。

[S1] 沿用既有 ST RM0390 依据：DMA DBM、地址更新、stream 中止 TC 与 ADC 章节。本轮不重新认证完整硬件；实际版本及实施代码由 R0/R1 存档。

[S2] 沿用 STM32F446xC/E datasheet（DS10693）的工作条件与 ADC 特性依据；目标板实际配置由风险门验证。
`https://www.st.com/resource/en/datasheet/stm32f446re.pdf`

[S3] 本轮针对性重新核对 FreeRTOS-Kernel V11.1.0 queue.c，xQueueGenericSend 成功分支：内部临界区、traceQUEUE_SEND、token 复制、临界区退出。钩子没有天然 veto 能力。
`https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/V11.1.0/queue.c`

[S4] 本轮重新核对 FreeRTOS-Kernel V11.1.0 portable/GCC/ARM_CM4F/port.c，特别是 xPortSysTickHandler 已有 trace 入口/两种出口与 PendSV 的职责。保留既有临界嵌套及 FPU 路径验收要求。
`https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/V11.1.0/portable/GCC/ARM_CM4F/port.c`

[S5] 沿用 ST 官方 HAL ADC/DBM 驱动依据。master 只用于定位，实际构建必须锁定；本轮未重新认证这两份源码为项目构建版本。
`https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Src/stm32f4xx_hal_adc.c`
`https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Src/stm32f4xx_hal_dma_ex.c`

[S6] 本轮针对性核对 V11.1.0 tasks.c 中任务选择后 traceTASK_SWITCHED_IN 的顺序及首次 scheduler start 的 switched-in。此前 tick/pending/notification 结论继续作为已批准契约保留。
`https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/V11.1.0/tasks.c`

[S7] 沿用上轮 CMSIS-Core PRIMASK 访问接口依据；该接口不自动保证调用者事件接线正确。
`https://arm-software.github.io/CMSIS_6/main/Core/group__Core__Register__gr.html`

[S8] 本轮新增定位：V11.1.0 portable/GCC/ARM_CM4F/portmacro.h，portEND_SWITCHING_ISR 的两种 trace 退出，以及 portYIELD_FROM_ISR 的展开；portYIELD 本身仅挂起 PendSV，不发退出 trace。
`https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/V11.1.0/portable/GCC/ARM_CM4F/portmacro.h`

[S9] 本轮核对 ST 官方 stm32f4xx_hal_rcc.h 中 __HAL_RCC_CLEAR_RESET_FLAGS 设置 RCC_CSR_RMVF 的实现和注释。此 master 地址不是项目版本锁；具体 HAL commit 与启动调用链由 R0/代码审查给出。
`https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Inc/stm32f4xx_hal_rcc.h`

在线核对日期：2026-09-10。选择 V11.1.0 是固定源码参照，不是最新版声明。此次证据是资料与文稿一致性核对，不是目标固件编译、ARM 屏蔽测量、上板成功或长期稳定性结果。
