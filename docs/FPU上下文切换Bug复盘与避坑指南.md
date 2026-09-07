# FPU 上下文切换 Bug 复盘与避坑指南

> 项目：STM32F407VET6 智能天气时钟（FreeRTOS + LVGL 8.3）
> 现象：**每次触摸校准完成后系统必然崩溃**（HardFault），裸机时代同样校准却一切正常。
> 结论：FreeRTOS 移植版本与工程编译配置（硬件浮点）不匹配 —— 移植代码缺少 FPU 上下文保存/恢复，属于**框架级缺陷**，与本项目业务代码无关。

---

## 一、问题现象

1. 按 KEY 进入四角触摸校准，校准全程正常（四点坐标、拟合系数都对，`[CAL] OK` 正常打印）；
2. `[CAL-E]` 打印后数毫秒内必崩，串口出现 `[FAULT]` 转储；
3. 故障转储特征：
   - 空闲任务 TCB 的 `pxTopOfStack` 变成固定值 `0x20002248`（每次崩溃都一样）；
   - 恢复出来的"假帧"里混有 `"IDLE"` 任务名字节、`0x6004A114`、`0xA0128DDC` 等垃圾；
   - 假帧中的 PC/LR 有时是合法的内核代码地址（`prvCopyDataFromQueue`、`prvGetNextExpireTime` 等）；
4. 此前曾怀疑：空闲任务 TCB 被野写入、DMA 乱写、栈溢出、printf 竞争 —— 全部排除。

---

## 二、排查过程（关键证据链）

| 步骤 | 手段 | 结论 |
|---|---|---|
| 1 | `fromelf` 反汇编故障 PC | 崩溃点全在 FreeRTOS 内核函数（队列收发/定时器守护），非业务代码 |
| 2 | `Project.map` 解析 RAM 布局 | 空闲任务 TCB/栈位于 `ucHeap` 内，栈在 TCB 下方，上方是空闲堆 |
| 3 | 串口哨兵（tick 钩子 + 空闲钩子 + 校准前后快照 + 心跳监控） | 校准前后 TCB 字段完全干净（无野写）；"假帧"内容 = 看门狗代码自己读到的 TCB 字段值（TCB 地址、pxStack、"IDLE" 名字、守护任务 TCB...） |
| 4 | 逐字节核对故障帧 | **不是野写入，是"寄存器/栈错位"** —— 帧内容与某段代码的寄存器值完全一致 |
| 5 | 读 FreeRTOS 移植源码 `port.c` | 是 **V9 时代老式 ARM_CM4 移植**：PendSV 只保存 `R4-R11`，**没有任何 FPU 指令** |
| 6 | 查工程编译配置（uvprojx） | `CPUTYPE("Cortex-M4") FPU2` = **硬浮点编译**，浮点运算真实使用 FPU |
| 7 | 机理闭环 | 见下节 |

---

## 三、根因分析

### Cortex-M4F 的 FPU 上下文切换机制

- FPU 寄存器分为两组：调用者保存的 `S0-S15 + FPSCR` 和**调用者保存的 `S16-S31`**；
- 硬件 **lazy stacking**：任务用过 FPU 后，`FPCCR.FPCA=1`；下一次异常入口（如 PendSV）时硬件自动把 `S0-S15+FPSCR`（18 个字）压入**当前任务的栈**；
- **`S16-S31` 和每任务自己的 `EXC_RETURN` 必须由移植代码（port.c）保存**；
- 异常返回时，`EXC_RETURN` 的 bit4（FPCA）决定是否**多弹 18 个字的 FPU 帧**。

### 崩溃机理

本工程移植代码（老式 ARM_CM4）在 PendSV 里只做：

```asm
stmdb r0!, {r4-r11}     ; 只保存 R4-R11，S16-S31 不存
...
bx r14                  ; 直接用 PendSV 自己的 EXC_RETURN 返回
```

配合硬浮点编译后：

1. **校准是工程里唯一使用浮点的地方**（`TP_Cal_Run` 的 scale/off 计算）→ 任务 FPU 变脏，`FPCA=1`；
2. 校准结束 UI 任务 `vTaskDelay(5)` → 切到**空闲任务**（空闲任务从不使用 FPU，其保存帧里没有 FPU 段）；
3. PendSV 以 `FPCA=1` 进入，`EXC_RETURN` 带 FPCA=1 → 返回空闲任务时硬件**多弹 18 个字（0x48 字节）的"FPU 帧"**——而空闲任务的帧里根本没有这段 → **PSP 被推高 0x88 字节，S0-S15 全部读成栈上的垃圾**；
4. 空闲任务带着**错位的 PSP** 和垃圾寄存器继续运行 → 其栈帧落在正常位置上方 0x88 处 → 钩子函数的返回地址槽被 `"IDLE"` 名字字（0x454C4449）覆盖 → `POP {pc}` 跳到 `0x454C4448` → **执行未定义指令 → HardFault**；
5. 假帧里的"内核代码地址 + TCB 字段 + IDLE 字节" = FPU 超弹时把 TCB/栈里的字节当寄存器弹出的结果。

### 为什么是"只有校准后才崩"

- 浮点使用 = 崩溃触发器：校准是工程里唯一用浮点的代码；
- 裸机时代无 FreeRTOS 上下文切换 → 无异常帧弹出 → 永不触发；
- 此前反复出现的 `0x20002248` = FPU 超弹后的固定落点，并非野写。

---

## 四、解决办法（代码级）

将 `Third_Lib/FreeRTOS/port/port.c` 升级为官方 FreeRTOS V10.3.1 **RVDS/ARM_CM4F** 移植版本（ARMCC 兼容语法）。关键改动：

### 1. 新增每任务 EXC_RETURN 定义

```c
#define portINITIAL_EXC_RETURN  ( 0xfffffffd )
```

### 2. `pxPortInitialiseStack`：任务帧中写入自己的 EXC_RETURN

```c
pxTopOfStack -= 5;                                            /* R12, R3, R2 and R1. */
*pxTopOfStack = ( StackType_t ) pvParameters;                 /* R0 */
pxTopOfStack--;
*pxTopOfStack = portINITIAL_EXC_RETURN;                       /* EXC_RETURN */
pxTopOfStack -= 8;                                            /* R11..R4 */
```

### 3. `vPortSVCHandler`：弹出任务自己的 EXC_RETURN

```asm
ldmia r0 !, { r4 - r11, r14 }   ; 原为 {r4-r11}，且不再 orr r14,#0xd
msr psp, r0
...
bx r14
```

### 4. `xPortPendSVHandler`：FPU 感知的保存/恢复

```asm
    mrs r0, psp
    isb
    ldr r3, =pxCurrentTCB
    ldr r2, [ r3 ]

    ; ---- 保存侧：出栈任务 FPCA=0 时保存 S16-S31 ----
    tst r14, #0x10                  ; FPCA 位测试
    it eq
    vstmdbeq r0!, {s16-s31}
    stmdb r0 !, { r4 - r11, r14 }   ; 连 EXC_RETURN 一起保存
    str r0, [ r2 ]

    stmdb sp !, { r0, r3 }
    mov r0, #configMAX_SYSCALL_INTERRUPT_PRIORITY
    msr basepri, r0
    dsb
    isb
    bl vTaskSwitchContext
    mov r0, #0
    msr basepri, r0
    ldmia sp !, { r0, r3 }

    ldr r1, [ r3 ]
    ldr r0, [ r1 ]
    ldmia r0 !, { r4 - r11, r14 }   ; 弹出入栈任务自己的 EXC_RETURN

    ; ---- 恢复侧：按入栈任务自己的 FPCA 决定是否弹 S16-S31 ----
    tst r14, #0x10
    it eq
    vldmiaeq r0!, {s16-s31}

    msr psp, r0
    isb
    bx r14
    nop
```

**核心设计**：每个任务的帧携带自己的 `EXC_RETURN`，恢复时用它自己的 FPCA 位决定硬件 FPU 帧的弹出 → **超弹不可能发生**。

### 验证

- 烧录后连续多次校准 + 重启后再次校准，全部通过，心跳稳定；
- 反汇编确认固件中存在 `VSTMDBEQ r0!,{s16-s31}` / `VLDMEQ r0!,{s16-s31}`。

---

## 五、避坑指南

1. **FreeRTOS 移植与编译配置必须匹配**
   - 用硬件 FPU（Keil CPU 选 `Cortex-M4 + FPU2`，即 ARMCC `--cpu=Cortex-M4.fp`）→ 必须使用 **ARM_CM4F 移植**（port.c 的 PendSV 含 `vstmdbeq/vldmiaeq`）；
   - 不用 FPU（`--cpu=Cortex-M4` 无 `.fp`，软浮点）→ ARM_CM4 移植即可；
   - 检查方法：打开 port.c 搜 `s16-s31` / `EXC_RETURN`，没有就是非 FPU 移植。

2. **掌握 FPCA 机制常识**
   - Cortex-M4F 的 FPU 上下文：硬件 lazy stacking 只负责 `S0-S15+FPSCR`；`S16-S31` 和每任务 `EXC_RETURN` 必须由移植代码处理；
   - `EXC_RETURN` bit4（FPCA）= 返回时是否弹 FPU 帧，**必须与入栈任务帧的实际内容一致**，否则栈指针错位。

3. **"只有特定时机才崩"的崩溃，优先怀疑上下文切换路径**
   - 本 bug 只在"用过浮点之后"崩（校准 = 唯一浮点）；裸机不崩（无上下文切换）；
   - 触发条件与某类资源的使用挂钩时（浮点/中断/DMA），先检查异常帧的保存/恢复对称性。

4. **故障转储的解读要点**
   - 假帧里出现 **TCB 名字字节/结构体字段值** = 典型的"寄存器/栈错位"信号，不是野写入；
   - 优先怀疑异常返回帧不对称（FPU 超弹、EXC_RETURN 不匹配），再查 DMA/野指针；
   - 崩溃 PC 落在内核函数（prvCopyDataFromQueue 等）≠ 内核有 bug，往往是恢复出的假寄存器在跑内核代码。

5. **排查工具链（本项目已验证有效）**
   - `fromelf --text -c` 反汇编故障 PC / 校验固件内容；
   - `Project.map` 解析 RAM 布局（ucHeap、TCB、栈的位置关系）；
   - 串口哨兵：tick 钩子 1ms 轻量检查 + 空闲钩子完整校验 + 操作前后快照对比 + 心跳监控关键字段。

---

## 六、下次遇到类似问题的快速处理 Checklist

1. **固定现场**：记录 `HFSR/CFSR/BFAR` + MSP/PSP 双帧的 `PC/LR/R0-R3`；
2. **反汇编 PC**：判断崩溃在业务代码还是内核代码；
3. **看假帧内容**：若含 TCB 名字节/结构体字段 → 栈错位嫌疑，检查 `port.c` 的 PendSV/SVC 是否有 FPU 指令；
4. **核对配置**：`Project.uvprojx` 的 `CPUTYPE/FPUx` 与移植版本是否匹配（FPU2 ↔ ARM_CM4F）；
5. **对比官方移植**：与 FreeRTOS 官方同版本 `portable/Keil/ARM_CM4F`（或 RVDS/ARM_CM4F）逐行 diff，确认 SVC/PendSV/pxPortInitialiseStack 三个函数一致；
6. **修复后按触发条件回归**：本案例 = 执行一次浮点运算后再做上下文切换（校准流程）。

---

## 七、相关文件

| 文件 | 说明 |
|---|---|
| `Third_Lib/FreeRTOS/port/port.c` | 已修复：FPU 感知的 SVC/PendSV/pxPortInitialiseStack（V10.3.1 CM4F 版） |
| `project/MDK(V5)/Project.uvprojx` | CPU = `Cortex-M4 + FPU2`（硬浮点） |
| `app/mytasks.c` | 调试用看门狗/心跳/校准快照已全部移除，保留 lv_switch 测试画面 + KEY 校准流程 |
| `module/stm32f4xx_it.c` | 故障转储（含空闲 TCB 区域转储） |
