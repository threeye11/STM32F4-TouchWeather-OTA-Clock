# SPI DMA 驱动 LCD 调试记录

## 背景

在 FreeRTOS 移植过程中，为 LCD 绘图函数引入 SPI DMA 传输（DMA2_Stream3, Channel3, SPI1_TX），以加速矩形填充和图片显示。结果 LCD 黑屏、无任何显示，经多轮排查定位到4个独立 bug。

## 硬件环境

- MCU: STM32F407VET6 (168MHz)
- LCD: ILI9341 240x320, SPI 接口 (SPI1)
- DMA: DMA2_Stream3 Channel3 (SPI1_TX)
- SPI 时钟: APB2/16 = 5.25MHz

---

## Bug 1：`SPI_I2S_DMACmd` 过早启用（致命）

### 现象

`LCD_Clear(WHITE)` 闪白后屏幕全黑，串口无输出。所有阻塞式 SPI 操作静默失败。

### 根因

`SPI_DMA_Init()` 在末尾调用了：

```c
SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
```

该函数在 `LCD_init()` 中被调用，**早于任何 DMA 传输**。启用 SPI TX DMA 请求后，SPI 外设的 TXE 标志行为改变（由 DMA 控制器管理而非 CPU），导致后续所有 `SPI_SendByte()` 中的 `while(TXE == RESET)` 死循环。

### 修复

将 `SPI_I2S_DMACmd` 从 `SPI_DMA_Init()` 移除，改为在 `SPI_DMA_SendBytes()` 中传输前启用，传输完成后立即禁用：

```c
// SPI_DMA_SendBytes() 中：
SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);  // 传输前启用
DMA_Cmd(DMA2_Stream3, ENABLE);

// SPI_DMA_WaitDone() 中：
while (DMA_GetCmdStatus(DMA2_Stream3) != DISABLE);  // 等待传输完成
SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);  // 传输后禁用
```

### 教训

- `SPI_I2S_DMACmd` 会改变 SPI 外设行为，不能提前启用
- 阻塞 SPI 和 DMA SPI 不能同时处于活跃状态

---

## Bug 2：`LCD_CS_SET()` 缺失（致命）

### 现象

`LCD_DrawRect_DMA` 和 `LCD_ShowImage_DMA` 执行后，后续所有 LCD 操作失效，屏幕黑屏。

### 根因

`LCD_DrawRect_DMA` 调用 `LCD_SetArea()` 时，内部 `LCD_WriteCmd` 会拉低 CS（`LCD_CS_CLR()`）。DMA 传输完成后没有调用 `LCD_CS_SET()` 释放片选：

```c
void LCD_DrawRect_DMA(...)
{
    LCD_SetArea(...);           // CS 被拉低
    // ... DMA 传输 ...
    for (row...) {
        SPI_DMA_SendBytes_Wait(...);
    }
    // ← 缺少 LCD_CS_SET()，CS 一直拉低！
}
```

CS 持续拉低导致 SPI 总线被 LCD 模块独占，后续所有 SPI 通信（`LCD_ShowString` 等）全部失败。

### 修复

在 DMA 循环结束后添加 `LCD_CS_SET()`：

```c
for (uint16_t row = 0; row < h; row++) {
    SPI_DMA_SendBytes_Wait(line_buf, pixels * 2);
}
LCD_CS_SET();  // 释放片选
```

### 教训

- `LCD_CS_CLR/SET` 必须配对使用
- DMA 函数不能假设调用者会管理 CS，必须自行保证完整生命周期

---

## Bug 3：DMA 等待依赖 ISR 但 ISR 不触发

### 现象

`LCD_DrawRect_DMA` 执行时系统挂起，串口停止输出。

### 根因

原实现用 `while(!SPI_DMA_Done)` 等待 DMA 完成，依赖 `DMA2_Stream3_IRQHandler` 中断设置标志位。但该 ISR 可能因 NVIC 配置、优先级冲突等原因不触发，导致永久等待。

### 修复

改为直接轮询 DMA Stream 使能位，绕过 ISR：

```c
static void SPI_DMA_WaitDone(void)
{
    uint32_t timeout = 0xFFFFF;
    while (DMA_GetCmdStatus(DMA2_Stream3) != DISABLE) {
        if (--timeout == 0) {
            // 超时处理
            DMA_Cmd(DMA2_Stream3, DISABLE);
            return;
        }
    }
}
```

DMA 在 Normal 模式传输完成后，硬件自动清除 EN 位，无需依赖中断。

---

## Bug 4：DMA 完成后 SPI 残留状态未清除

### 现象

`LCD_DrawRect_DMA` 单独使用正常，但与 `LCD_ShowString` 组合时屏幕黑屏。

### 根因

SPI 是全双工外设，DMA 每发送一个字节，同时在 RXNE 中产生一个接收字节。DMA 传输完成后：

- RXNE 标志仍置位（残留接收数据未读取）
- BSY 标志可能仍置位（最后一个字节正在移位）

后续 `SPI_SendByte()` 中 `while(RXNE == RESET)` 立即返回（读到脏数据），导致 SPI 通信数据错乱。

### 修复

DMA 传输完成后刷新 SPI 状态：

```c
// 等待总线空闲
while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
// 清除 RXNE 残留
if (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE))
    SPI_I2S_ReceiveData(SPI1);
```

### 教训

- SPI 全双工模式下，TX DMA 会同时产生 RX 数据
- DMA 和阻塞模式切换时必须清理外设状态

---

## 修复后的完整 DMA 流程

```
LCD_DrawRect_DMA()
  │
  ├─ LCD_SetArea()          ← CS 拉低，设置绘图区域
  ├─ 填充 line_buf
  │
  ├─ for each row:
  │   ├─ SPI_DMA_SendBytes()
  │   │   ├─ SPI_DMA_WaitDone()        ← 等待上次传输完成
  │   │   ├─ while(BSY)                ← 等待总线空闲
  │   │   ├─ 读 RXNE 清残留
  │   │   ├─ 设置 M0AR, NDTR
  │   │   ├─ SPI_I2S_DMACmd(ENABLE)    ← 启用 DMA 请求
  │   │   └─ DMA_Cmd(ENABLE)           ← 启动传输
  │   │
  │   ├─ [DMA 传输480字节]
  │   │
  │   └─ SPI_DMA_WaitDone()
  │       ├─ 轮询 EN 位直到 DISABLE     ← 传输完成
  │       ├─ SPI_I2S_DMACmd(DISABLE)   ← 关闭 DMA 请求
  │       ├─ 清除 TC 中断标志
  │       ├─ while(BSY)                ← 等待总线空闲
  │       └─ 读 RXNE 清残留             ← 恢复阻塞 SPI 模式
  │
  └─ LCD_CS_SET()            ← 释放片选
```

## 关键设计原则

1. **阻塞 SPI 和 DMA SPI 不能共存**：`SPI_I2S_DMACmd` 必须在传输前启用、传输后禁用
2. **CS 生命周期必须完整**：谁拉低谁释放，DMA 函数内部管理
3. **模式切换要清理状态**：DMA → 阻塞切换时需清除 RXNE、等待 BSY
4. **优先轮询硬件标志**：DMA EN 位比软件 ISR 标志更可靠
