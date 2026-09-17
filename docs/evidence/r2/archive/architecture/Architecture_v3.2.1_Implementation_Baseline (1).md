# STM32 实时流式系统架构 v3.2.1
## 三个适配器局部勘误合并稿：tick 服务、完成提交、CPU 计量

**日期：2026-09-10**  
**平台：NUCLEO-F446RE / STM32F446RE / FreeRTOS / C、受约束 C++ / Python**  
**状态：沿用 v3.2 主架构作为实现基线；本稿新增适配器契约待代码审查与定点测试，不等于已通过开发板验收。**

本稿是 v3.2 的小版本合并稿，不是重新选择架构。依据本轮外部评审 [R32]，保留已经成立的主数据路径、K+2 池、四任务、启停与 cohort 定义，只补齐三个适配器契约及少量实施边界。章节主编号仍为 1—17。与 v3.2 冲突时以本稿为准；附录 A 列明 v3.1 保留内容的继承位置。下文“必须”“上限”“验收目标”均为项目要求，不是已测性能或 ST/FreeRTOS 提供的项目保证。

来源标记：[R] 为上一轮 v3.1 评审，[R32] 为本轮 v3.2 评审；[S1]—[S6] 沿用原稿来源，[S7] 为 CMSIS 中断屏蔽接口说明。本轮重新核对了 [S3]、[S4]、[S6] 的固定版本源码及 [S7]；没有重新做整板硬件认证。评审建议、已核实的内核行为和本稿选定的实现约束分别标明。本文没有 ARM 固件编译、上板测试或真实性能测量结果。

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

钩子必须只匹配本项目 FreeBufferQueue 和显式标记的 `COMPLETE` 操作。初始化填充、`CANCEL` 归还、其他队列发送不能被误计为正常完成。钩子不得调用 FreeRTOS API，不得计算 DSP，不得调用同一队列，不得分配内存。失败发送没有正常完成提交；意外满队列属于基础设施错误。

### 4.3 完整不可抢占区间与时间含义

本轮补齐临界段的起点，而不是只描述 t_commit 之后的尾段。[R32][S3][S4]

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

### 4.5 操作分流与构建约束

正常 COMPLETE、取消 CANCEL、初始化 INIT、非法操作四条路径分别测试。只有合法 COMPLETE 可以产生一次正常结果；CANCEL/INIT 只能执行其对应账目变更。未声明来源的 FreeBufferQueue 发送一律非法。

取消路径使用 `CancelAndReleaseBlock`。专用钩子是功能适配器，不是可随普通诊断 trace 一起移除的观察点。仓库必须提供唯一宏定义入口、版本/源码 hash 检查及 Benchmark 构建检查，防止第三方 recorder 或空默认宏覆盖它。关闭普通 trace 后仍须运行完成/归还不变量测试；没有这些证据，不批准该适配器冻结。

## 5. F2：统一等待、START 与 STOP

### 5.1 Processing 只在一个通道阻塞

ReadyQueue 仍保存真实描述符，但 Processing 只使用零等待 receive；无工作时统一阻塞在任务通知上。通知位为 `WORK | STOP | START`，持久存在，不要求一条通知对应一个 block。

ISR 按“先成功发布描述符、后置 WORK 位”的顺序操作。Processing 醒来后以队列为准逐块取，取下一块前检查停止闸门。队列空和准备等待之间到达的通知必须被保留，不得通过错误清零丢失。

STOP 使用同一个通知通道。这样不再依赖“通知能唤醒正在等待队列的任务”这个不成立的假设。[S6]

`TryClaimNextBlock` 规定认领的串行化点：若 STOP 先提交，该块只能取消；若 Processing 已先提交当前块认领，允许完成该当前块。取出但尚未确认认领的描述符仍需归还，不得消失。

### 5.2 START 准备与最终提交分开

Communication 是生命周期唯一协调者。只有上一 run 所有权回收完毕且两个工作任务均已停驻才允许 START。

准备阶段完成：配置验证、boot/run/config/prediction 绑定、队列及已识别旧运行通知的整理、FIR state 与 previous sequence 复位、干扰 pending state 复位、统计复位、时钟及外设配置、TIM 装载与 UG、旧标志清理、ADC/DBM 武装、DAC 启动及稳定准备。通知整理必须早于发布新启动票据；worker 的旧执行尾部不得再清除新 START 通知。

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

### 6.2 固定 TAIL，不再保留两种冲突结束策略

本基线只使用预先配置的固定 tail_blocks，默认 8，最少 2。不在 cohort 全部完成时提前切换负载。TAIL 中采样、干扰、平台任务与优先级保持不变，新增输入不属于 primary cohort 但继续制造真实负载。

令 `S2 = S1 + tail_blocks`。在输入块事件 S2 的 DMA ISR 入口定义 observation cutoff。“输入块事件”指事件完整性检查通过的块完成，包含最终 capacity-drop 的输入块，绝不是仅指 admitted 块；S0/S1/S2 边界处理均先于接纳成败分支。进入此 cutoff 前应满足：

\[
t_{obs} > \max_{k\in cohort}(t_{nominal,k}+D) + \epsilon_{time}.
\]

其中 epsilon_time 覆盖已声明的 epoch/commit 不确定度。配置阶段检查预期条件，运行时再核对。若不成立，记录 `INSUFFICIENT_OBSERVATION`，不得把未到期未完成块算成 deadline failure。

### 6.3 完成与截止采用同一排序

`CloseObservation` 与完成钩子处于同一串行化规则下。截止只读取已经提交的完成结果；未提交的至多 K 个 cohort 驻留块被一次性标记为 `EXPIRED_UNRESOLVED`。完成段受保护时截止不得进入其中；截止先提交后，晚到完成只能影响资源回收与诊断，不能修改已关闭的 cohort 分类。

同周期时间戳相等时，事件序号确定先后；结果中必要时记录提交序列以便短 trace 核对。模型使用一致的串行化顺序，而不是用任意容器遍历顺序决定分类。

在 S2 截止操作后才开始停止事务；停止过程引起的调度改善不纳入已关闭的 cohort 结果。截止所在 ISR 只做有界分类关闭、闸门设置和停止请求。等待 DMA EN=0、等待 worker ACK、报告封存与发送等可阻塞或较长工作仍由 Communication 协调，禁止将完整 STOP 流程搬入 ISR。FaultTest.Stall 使用第 12.1 节的独立停止政策，不执行这里的普通测量 cutoff 自动 STOP。

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

Clock64 单调不等于 runtime ledger 已安全。本轮禁止下列分离写法：[R32]

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

## 8. 百分位数与占用统计

基线 latency histogram 为 128 bins、范围 0—8D，另设 overflow。bin width 为 D/16；输出为区间或带分辨率标注的估计，而非精确分位点。

必须区分两个问题：

- histogram overflow：完成延迟超出直方图覆盖范围；
- unresolved censoring：最慢的一部分 admitted block 尚未完成，根本没有延迟样本。

默认输出字段为 `p99_completed_by_cutoff`，其统计对象是 cutoff 前完成的 cohort blocks。同时输出 N_admitted、N_completed、N_unresolved 和 overflow。

只要存在 unresolved，基线就不声称此值是全体 admitted 的 P99；全体字段返回 N/A/CENSORED。完成样本本身也有 overflow 时，用秩检查判断分位点是否可辨识：若目标秩落入 overflow，输出下界或 OUT_OF_RANGE，不能填成 8D。没有完成样本时返回 N/A。

arrival-observed occupancy 指 DMA admission 决策前观察到的 Q，不解释为时间加权占用。模型经过相同 population 筛选和分箱后再比较。

## 9. tick 服务序号、启动与干扰释放

### 9.1 服务序号不是内核 tick

本轮已核对 V11.1.0：scheduler suspension 期间 tick hook 仍执行，但 xTickCount 暂不推进；恢复时补算 pending ticks 又避免重复执行 hook。xTaskGetTickCountFromISR 返回的是内核计数。故 hook 次数与该 API 的连续读值不可混用。[S6]

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

冻结成本表、phase uncertainty、admission offset 与模型代码后，先存 prediction artifact，再 START。该轮硬件测得的实际工作量只能出现在另行标记的 post-hoc replay 中，不得替换预测输入。

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

增加一个配置类别，不增加子系统：`run_kind = PERFORMANCE | FAULT_STALL`。PERFORMANCE 保留固定 WARMUP/MEASURE/TAIL、S2 自动停止和事前预测流程；其中 INJECT_STALL 非法。

FAULT_STALL 的启动仍使用同一安全 START、DBM、四任务和健康规则，但不执行普通 cohort 截止自动 STOP。它只用于验证真实停工后的 IWDG 路径，不进入模型精度数据集。注入命令必须绑定 run/request 并幂等；它仅使 Processing 在确定测试点真实停止，不直接命令 Monitor 停喂。

Host 在测试前固定观察超时：至少覆盖最大合法无进展检测时间、按 LSI 容差得出的 IWDG 上界、启动/复位原因报告上界、传输余量。没有这些上界，不接受一个声称能验收 watchdog 的配置。观察期内不因普通命令响应超时发 STOP，也不运行普通 STOP grace 定时器抢先软件复位。

实际 IWDG reset、正确复位原因和新 boot/run 握手全部取得才通过。观察超时、基础设施失败先复位、软件复位或人工 STOP 均分别记录 TIMEOUT/INVALID/ABORTED，不能算作 IWDG 恢复成功；记录失败后可以执行明确的人工/软件恢复操作，不能追溯改变测试结论。

安全错误处理始终有效；此 profile 不隐藏 DMA/内存错误，也不允许无界有效实验。测试结束后仍回到既有 BOOT/IDLE 生命周期。特别测试“在原普通 cutoff 附近注入”，证明被测 IWDG 路径没有被普通 STOP 抢先结束。[R32]

## 13. 通信与结果绑定

采用有界二进制帧。候选线规为 Magic、版本、类型、请求 ID、长度、payload、CRC-32；最大 payload 256 B。CRC参数、字节序和测试向量写入 protocol.md，C/Python 必须共享 golden frames。

RX 有长度检查、丢字节/溢出计数、有限重同步和处理预算；错误帧不能改变运行状态。USART 8N1、115200 为 bring-up 起点，Benchmark 不以串口速度为实验变量。正式运行只允许 PING/小 STATUS/STOP；GET_RESULT 在封存后分块取回。

控制流量安全验收 profile：每秒最多 10 个小控制请求、每个运行期请求 payload 不超过 64 B；覆盖许可范围内的不利突发，不只测均匀间隔。超预算输入拒绝/限流并记录，不以无限高优先级解析压垮采集。正式模型验证默认采用第 11.4 节 QUIET profile；FAULT_STALL 另外允许一次有效的 INJECT_STALL，不与 PERFORMANCE 命令表混用。

START 绑定 boot/session、request_id、config_hash、prediction_id。ACK 丢失后同一 START 重试返回同一 run，不再开始一轮。STOP 重试返回停止进度或同一最终结果；只有收到双方 worker ACK 和 DMA EN=0 后才返回 STOPPED，而不是把“已接受 STOP 请求”当成“已经 IDLE”。目标发生 reset 时旧 boot 的请求不能自动启动新 run。

UART TX DMA 使用专用、固定大小 staging buffer。DMA 尚未确认停止访问前，CPU 不得修改或复用其中内容，也不能让下一 START 的统计复位覆盖它。每个结果片段绑定 boot_id、run_id、result_id、片段号及总长度。

本版选择有界保留策略：前一封存结果仍有传输/下载引用时，不允许复用其存储；新 START 可返回 RESULT_BUSY。只有主机已明确消费或放弃旧结果、相关 TX DMA 已结束，才允许复用。无需为任意多历史 run 保留 MCU 内存；长期存储仍在 PC。普通短状态帧也遵守同一 TX 所有权规则。

无效和 aborted runs 同样存盘，正式模型误差汇总显式排除并列出数量与原因。

## 14. 预先固定的实验与模型评价

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

必要交付文件：architecture.md、hardware-map.md、buffer-contract.md、run-lifecycle.md、timing-and-statistics.md、protocol.md、SETUP.md、ERRATA_APPLICABILITY.md、dependency-locks、calibration/validation manifests、测试与原始结果。

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
| T12 | >60 s、嵌套 IRQ、DWT回绕 | Clock64 单调、计量无重复归属、关闭结果不再写入 |
| T13 | START ACK 丢失、重复 START/STOP、目标 reset | 不重复启动、不串 run、不把旧预测配新结果 |
| T14 | deadline 附近与 histogram overflow/unresolved | 标注精度、分母与population，不伪造 P99 |
| T15 | 短 scheduler suspension 分别跨越 q0、release；resume 补算 | service_seq 一次一进；启动不漏、释放不漏不重；DWT 服务异常不能补跑 |
| T16 | DMA 在 t_lock 前、临界前缀、t_commit、解锁附近到达；四种发送路径 | 尊重完整屏蔽段；COMPLETE/CANCEL/INIT/FAULT 不混算；错误 token 不被接纳 |
| T17 | 低优先级计量取时后、账目写入前使高优先级 IRQ pending；同时测试窗口关闭 | RuntimeEvent 完整串行化，无旧时间回灌、下溢、双计或关闭字段再写 |
| T18 | S0、S1、S2 输入事件恰好全部被 capacity-drop | 窗口边界按输入事件推进，不依赖成功入队 |
| T19 | FaultTest 在原普通截止附近注入；主机响应超时；软件复位反例 | 只能凭实际 IWDG 复位原因通过，普通 STOP/软件复位不能冒充 |
| T20 | 结果 TX 尚未结束时重复 GET_RESULT / START / 释放旧结果 | 在途字节不可变、结果不串 run；有界 RESULT_BUSY 与释放规则有效 |

R0：时钟、电源、采样设置、中断全集、工具链与资料版本。  
R1：采集节奏、ADC/DBM小驱动、部分块停止及中止 TC。  
R2：K+2池、完成钩子、接纳/drop、最坏获准负载与服务余量。  
R3：完整启停、空等待、pending工作、失败回滚及定点交错；然后重复启停至少1000次。  
R4：Clock64、runtime窗口、逻辑提交误差、相位、嵌套与cutoff一致性。  
R5：合成已知结果贯通 warmup/cohort/tail/封存，守恒与分类同时核对。  
R6：成本边界、延迟admission、skip机制和prediction-before-run验证。  
R7：DSP数字与模拟路径验证，随后最终Benchmark固件回归 R2—R4。

本轮只扩展现有关口：T15→R0/R4/R6，T16→R2/R6，T17→R4，T18→R5，T19→既有 watchdog 验收，T20→R3/通信验收。T13 额外覆盖最后一个 QUIESCED ACK 后立即重启；如结果传输尚占用存储应先得到 RESULT_BUSY，解除引用后必须验证真正接受 START 的交错。普通 trace 关闭的最终 Benchmark 构建也执行 T01/T16。

随机长跑不能替代上述定点测试。初期可以开展 R0—R2 原型和独立微基准；正式统计、批量独立验证及“已冻结实现接口”的声明分别受相应门约束。

## 17. 本轮评审闭环、实施边界与版本索引

### 17.1 不重新打开已经成立的主契约

本轮外部评审 [R32] 建议批准 v3.2 主架构作为实现基线。完成/可见性分离、统一停止协议、固定 TAIL、cohort 分类与全路径 DBM 余量继续保留；“文档契约足以实现”不等于“代码已正确/板卡已通过”。

| 本轮内容 | v3.2.1 处理 | 对应冻结证据 |
|---|---|---|
| C1 服务 tick 与内核 tick 不同 | 9.1—9.5：独立 service_seq、tickless=0、统一 q0/release、DWT 交叉检查 | 固定版本源码检查；T15、R0/R4/R6 |
| C2 完成临界段与错误路径 | 4.3—4.5：t_lock/commit/unlock、禁止外层屏蔽、非 veto 错误关闸、epilogue 成本 | 适配器/反汇编审查；T01/T16、R2/R6 |
| C3 计量事件整体原子性 | 7.4：Clock64 与 ledger 共用短 RuntimeEvent；窗口事件相同规则 | T12/T17、R4 |
| ACK 后立即启动 | 5.4：ACK 为资源承诺，保留新通知，不依赖任务物理 Blocked | 扩展 T13、R3 |
| 窗口边界丢块 | 6.2/7.1：输入事件包括 drop | T18、R5 |
| Watchdog 与正常 cutoff 冲突 | 12.1：FaultTest.Stall 独立停止政策 | T19、实际 reset cause |
| 通信输入与 TX 生命周期 | 11.4/13：静默预测、压力验收分离；不可变 TX 与有界结果保留 | T20、协议测试 |

**设计决策：主架构按本基线推进实施；三个局部适配器的契约已经补写，仍待最小代码、定点测试与相应硬件证据后冻结。**没有主张 F446RE 已获得全范围运行认证，也没有把本稿中的余量目标称为实测结果。

不新增 R8，不升级到自制队列/调度器，不要求为了下一轮文字评审暂停所有模块。可并行开展已经有明确接口的 host 协议、配置验证、数字 DSP 测试和早期采集原型；依赖精确提交、相位和 CPU 归属的正式结论分别受 C1—C3 和既有风险门约束。

下层优先提交三个小型实现包：TickServiceAdapter、CompleteAndReleaseBlock 适配器、Clock64/RuntimeEvent。每包包含源码、版本/构建绑定、上述反例的自动测试及待上板测量清单。后续评审以这些证据为输入，不以继续扩充架构篇幅代替实现。

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
| 结果公共字段 | 保留 boot/run/config/prediction/schema ID、输入/接纳/drop/完成/过期未完成计数、明确分母的 rate/yield、条件 latency histogram、占用、独立 CPU wall window、runtime/误差及完整性状态，§6—8/13—14 |
| 预测先于验证、冻结校准、模型允许误差 | 保留，§11/14；验证后 replay 单列，不替换已保存预测 |
| 依赖、静态内存、errata、协议字节格式 | §2/13/15 保留约束，具体实现清单下沉到 SETUP/protocol/ERRATA_APPLICABILITY/版本锁；未写出的细节不得由不同开发者各自定义 |
| T01—T14、R0—R7 | 全部保留；增加 T15—T20 定点案例并扩展已有 gate，不另增主要阶段 |

本版不因压缩篇幅删除旧的有效要求，也不恢复被 v3.2 明确替换的 t_finish、含糊内核 tick 或停止顺序。遇到此索引未能消除的实质冲突，应暂停对应局部接口定版，而不是任意选择一个旧版本实施。

---

## 参考资料与证据边界

[R] 用户上传的 v3.1 评审，文件“粘贴的 markdown (1)。md(5)”；沿用 F1—F5 闭环。

[R32] 用户本轮上传的 v3.2 评审，文件“粘贴的 markdown (1)。md(6)”。本轮三个适配器修订及实施边界以此为输入。评审的通过建议、本文设计选择和未完成的硬件验证不得混为一谈。

[S1] 沿用 v3.2 的 STMicroelectronics RM0390 依据：重点 §9.3.10、§9.3.14—9.3.15、DMA 地址寄存器限制及 ADC 章节。本次局部勘误没有重新审计全部硬件章节；R0/R1 仍需存档实际采用版本并验证。

[S2] 沿用 v3.2 的 STM32F446xC/E datasheet（DS10693）工作条件及 ADC electrical characteristics 依据；不是本轮新取得的板级认证。R0 必须存档实际使用版本与配置。
`https://www.st.com/resource/en/datasheet/stm32f446re.pdf`

[S3] 本轮重新核对 FreeRTOS-Kernel V11.1.0，queue.c，xQueueGenericSend 中临界区入口先于 traceQUEUE_SEND，后续仍复制 token 再退出。对应取回源码约第 859—967 行；正式复核以函数和源码 hash 为准。MIT 开源源码，本文未复制整段实现。
`https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/V11.1.0/queue.c`

[S4] 本轮重新核对 FreeRTOS-Kernel V11.1.0，portable/GCC/ARM_CM4F/port.c，vPortEnterCritical/vPortExitCritical 与 xPortSysTickHandler（约第435—459、517—540行），包括嵌套与受保护 SysTick 路径。
`https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/V11.1.0/portable/GCC/ARM_CM4F/port.c`

[S5] 沿用 v3.2 的 ST 官方 stm32f4xx-hal-driver 源码依据；HAL_ADC_Start_DMA、HAL_DMAEx_MultiBufferStart_IT、HAL_DMAEx_ChangeMemory。以下 master 地址用于定位，实际构建必须锁定版本；本轮未将这些地址重新认证为具体构建版本。
`https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Src/stm32f4xx_hal_adc.c`
`https://raw.githubusercontent.com/STMicroelectronics/stm32f4xx-hal-driver/master/Src/stm32f4xx_hal_dma_ex.c`

[S6] 本轮重新核对 FreeRTOS-Kernel V11.1.0，tasks.c，xTaskIncrementTick 正常/暂停路径、pending tick 回放保护，以及 xTaskGetTickCountFromISR 读取 xTickCount（约第4265—4272、4407—4475、3748—3777行）。通知与既有生命周期判断仍沿用相同固定版本。
`https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/V11.1.0/tasks.c`


[S7] 本轮核对 CMSIS-Core：Core Register Access，__get_PRIMASK、__set_PRIMASK、__disable_irq。它们提供接口语义，不自动证明整个 RuntimeEvent 已正确组织。
`https://arm-software.github.io/CMSIS_6/main/Core/group__Core__Register__gr.html`

资料核对日期：2026-09-10。选用 V11.1.0 是固定源码参照，不是“当前最新版”声明。
