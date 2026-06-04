# S800 智能联网时钟系统 — 完整项目文档

> **上海交通大学"嵌入式系统"课程项目**
>
> 基于 TM4C1294NCPDT (S800 Board) 的双向协作多功能数字时钟。
> 板端独立运行完整的时钟/闹钟/编辑功能，PC 端通过 USB 虚拟串口实现远程控制与 1:1 数字孪生镜像。

---

## 目录

- [1. 项目结构总览](#1-项目结构总览)
- [2. 根目录文件说明](#2-根目录文件说明)
- [3. firmware/ — 板端固件（C 语言）](#3-firmware--板端固件c-语言)
  - [3.1 模块功能速查](#31-模块功能速查)
  - [3.2 模块间调用关系](#32-模块间调用关系)
  - [3.3 每个文件详解](#33-每个文件详解)
- [4. pc_app/ — PC 上位机（Python）](#4-pc_app--pc-上位机python)
  - [4.1 模块功能速查](#41-模块功能速查)
  - [4.2 线程与信号架构](#42-线程与信号架构)
  - [4.3 每个文件详解](#43-每个文件详解)
- [5. driverlib/ 和 inc/ — TivaWare 驱动库](#5-driverlib-和-inc--tivaware-驱动库)
- [6. RTE/ — Keil 运行时环境](#6-rte--keil-运行时环境)
- [7. 快速上手](#7-快速上手)
  - [7.1 编译烧录固件](#71-编译烧录固件)
  - [7.2 运行 PC 上位机](#72-运行-pc-上位机)
  - [7.3 板端独立使用](#73-板端独立使用)
- [8. 功能使用指南](#8-功能使用指南)
  - [8.1 板端按键操作](#81-板端按键操作)
  - [8.2 PC 上位机操作](#82-pc-上位机操作)
  - [8.3 串口通信协议](#83-串口通信协议)
  - [8.4 E1: NTP 一键对时](#84-e1-ntp-一键对时)
  - [8.5 E2: 天气获取与短显](#85-e2-天气获取与短显)
- [9. 环境变量配置](#9-环境变量配置)
- [10. 故障排查](#10-故障排查)

---

## 1. 项目结构总览

```
项目根目录/
├── .env.example              ← 环境变量模板（复制为 .env 使用）
├── .gitignore                ← Git 忽略规则
├── PRD.md                    ← 产品需求规格说明书
├── README.md                 ← 项目简介（英文）
├── PROJECT.md                ← 本文件：完整项目文档
├── requirements.txt          ← Python 依赖列表
├── check.py                  ← 独立串口调试工具
├── S524031910727.uvprojx     ← Keil MDK 工程文件
│
├── firmware/                 ← 【板端固件】C 语言，烧录到 S800
│   ├── hw_config.h           ← 硬件定义中枢（引脚/I2C/UART/常量）
│   ├── main.c                ← 主程序：初始化、开机动画、主循环、ISR
│   ├── clock.h / clock.c     ← 时钟模块：走时、进位、闰年/月末
│   ├── alarm.h / alarm.c     ← 闹钟模块：触发检测、响铃状态
│   ├── display.h / display.c ← 显示模块：7 段码扫描、流水、格式控制
│   ├── keys.h / keys.c       ← 按键模块：10 键扫描、编辑状态机
│   ├── buzzer.h / buzzer.c   ← 蜂鸣器模块：节奏控制、限时
│   ├── led.h / led.c         ← LED 模块：8 位指示灯管理
│   ├── protocol.h / protocol.c ← 协议模块：串口命令解析（容错三件套）
│   └── events.h / events.c   ← 事件模块：主动上报、心跳
│
├── pc_app/                   ← 【PC 上位机】Python，运行在电脑上
│   ├── __init__.py           ← 包初始化
│   ├── main.py               ← 程序入口 + 暗色主题
│   ├── main_window.py        ← 主窗口：布局、信号连接、扩展功能
│   ├── config.py             ← 配置常量
│   ├── protocol.py           ← 协议解析器
│   ├── serial_worker.py      ← 后台串口线程 (QThread)
│   ├── twin_state.py         ← 数字孪生状态镜像
│   ├── heartbeat.py          ← 心跳超时监控
│   ├── ntp_client.py         ← [E1] NTP 时间同步客户端
│   ├── weather_client.py     ← [E2] 天气 API 客户端
│   └── widgets/
│       ├── __init__.py       ← 组件导出
│       ├── seven_seg.py      ← 自绘 7 段数码管 (QPainter)
│       ├── led_indicator.py  ← LED 指示灯组件
│       ├── log_panel.py      ← 彩色收发日志面板
│       └── control_panel.py  ← 命令控制面板（SET/GET/演示/E1/E2）
│
├── driverlib/                ← TivaWare 外设驱动库（Ti 官方）
│   ├── gpio.c/h, i2c.c/h, uart.c/h, sysctl.c/h, ...  ← 源码
│   └── rvmdk/driverlib.lib   ← Keil 预编译库（可选）
│
├── inc/                      ← TM4C129 硬件寄存器定义头文件
│   └── hw_gpio.h, hw_i2c.h, hw_memmap.h, ...
│
├── RTE/                      ← Keil MDK 运行时环境（自动生成）
│   └── Device/TM4C1294NCPDT/
│       ├── startup_TM4C129.s ← 启动汇编
│       └── system_TM4C129.c  ← 系统初始化
│
├── Objects/                  ← Keil 编译产物（.o/.axf/.hex，自动生成）
└── Listings/                 ← Keil 链接产物（.map/.lst，自动生成）
```

---

## 2. 根目录文件说明

| 文件 | 作用 | 何时需要 |
|:---|:---|:---|
| **`.env.example`** | 环境变量配置模板，含 NTP 服务器、天气 API Key 等。复制为 `.env` 后编辑 | PC 上位机启动前，如需 NTP/天气功能 |
| **`.gitignore`** | 禁止 `.venv`、`Objects/`、`Listings/`、编译产物等进入版本控制 | 始终需要 |
| **`PRD.md`** | 产品需求规格说明书，是开发的基线文档 | 了解需求、检查功能是否齐全时查阅 |
| **`README.md`** | 英文项目简介，含快速开始和协议参考 | 外文读者或快速查阅 |
| **`PROJECT.md`** | **本文件**——最详细的中文项目文档 | 开发、调试、交付时的主要参考 |
| **`requirements.txt`** | PC 上位机的 Python 依赖：PyQt5、pyserial、python-dotenv、ntplib、requests | 首次配置 PC 环境时 `pip install -r requirements.txt` |
| **`check.py`** | 独立串口调试脚本：扫描 COM 口、手动发指令、查看原始返回 | 调试串口通信、验证板端 UART 是否正常时使用 |
| **`S524031910727.uvprojx`** | Keil MDK 工程文件，双击打开即可编译固件 | 编译/烧录固件时在 Keil 中打开 |

---

## 3. firmware/ — 板端固件（C 语言）

> **编译目标**：TM4C1294NCPDT | **编译器**：ARMCC V5.06 | **语言标准**：C89
>
> **火焰流程**：入口 `main.c` → `SysTick_Handler`(1ms) 驱动所有定时任务 → `UART0_Handler` 喂协议解析器 → 主循环调度 1ms/10ms/100ms/1000ms 任务

### 3.1 模块功能速查

```
┌───────────────────────────────────────────────────────────┐
│                      main.c (主控)                         │
│  初始化 → 开机动画 → 主循环 → ISR (SysTick + UART0)       │
└────┬──────┬──────┬──────┬──────┬──────┬──────┬───────────┘
     │      │      │      │      │      │      │
     ▼      ▼      ▼      ▼      ▼      ▼      ▼
  clock   alarm  display  keys  buzzer  led   protocol
  (走时)  (闹钟) (显示)  (按键) (蜂鸣) (指示) (串口)
                                            │
                                            ▼
                                         events
                                         (上报)
```

### 3.2 模块间调用关系

| 调用方 → 被调用方 | 关系 |
|:---|:---|
| `main.c` → 所有模块 | 初始化(`_Init`)、主循环任务调度、ISR 分发 |
| `main.c` → `hw_config.h` | 所有硬件常量、extern 全局变量、I2C/UART 工具函数声明 |
| `keys.c` → `clock.c`, `alarm.c`, `display.c` | 编辑时读写时钟/闹钟值、切换显示模式 |
| `protocol.c` → `clock.c`, `alarm.c`, `display.c`, `keys.c`, `buzzer.c`, `led.c` | 命令执行：对时、设闹钟、显示控制、模拟按键、蜂鸣、LED 控制 |
| `events.c` → `protocol.c` | 通过 `Protocol_SendResponse` 发送事件报文 |
| `buzzer.c` → `led.c` | 蜂鸣器与 LED 共用 PCA9557 P07，输出需合并 |
| `display.c` → (独立) | 仅依赖 `hw_config.h` 中的 I2C 写函数 |

### 3.3 每个文件详解

---

#### `firmware/hw_config.h` — 硬件定义中枢（179 行）

**作用**：整个固件的唯一定义源。所有 GPIO 引脚、I2C 地址、寄存器常量、模块间共享的全局变量声明都在这里。

**关键常量**：
| 类别 | 定义 | 值 | 说明 |
|:---|:---|:---|:---|
| 系统时钟 | `SYSTEM_CLOCK_HZ` | 20,000,000 | 20MHz，与 exp2.c 一致 |
| SysTick | `SYSTICK_FREQUENCY` | 1000 | 1ms 时基 |
| I2C 地址 | `TCA6424_I2CADDR` | 0x22 | 已验证值（PRD 中为 0x44，实际以 exp2.c 为准） |
| I2C 地址 | `PCA9557_I2CADDR` | 0x18 | 已验证值（PRD 中为 0x38，实际以 exp2.c 为准） |
| GPIO 按键 | `KEY1_PIN` ~ `KEY4_PIN` | PF0, PJ0, PJ1, PN0 | K1(FUNC)~K4(SAVE) |
| 扩展按键 | `KEY5_BIT` ~ `USER2_BIT` | 0~5 | TCA6424 Port0 位 |
| LED 位 | `LED_HEARTBEAT` ~ `LED_DAYNIGHT` | 0~7 | PCA9557 输出位 |
| UART | `UART_BAUD_RATE` | 115200 | 8N1 无流控 |

**何时修改**：更换硬件连接时（换 I2C 地址、换 GPIO 引脚、改波特率）。

---

#### `firmware/main.c` — 主程序（820 行）

**作用**：系统入口，包含 `main()`、`SysTick_Handler()`、`UART0_Handler()`、I2C 底层函数、开机动画状态机。

**核心内容**：

| 函数 | 功能 |
|:---|:---|
| `main()` | 时钟初始化(20MHz) → SysTick 配置 → GPIO/I2C/UART 初始化 → 模块 Init → 开机动画 → 主循环 |
| `SysTick_Handler()` | 1ms 中断：置 `g_flag_1ms`，级联产生 10ms/100ms/1000ms 标志 |
| `UART0_Handler()` | 接收中断：读 FIFO 字符 → 喂 `Protocol_CharReceived()` |
| `I2C0_WriteByte()` | I2C 写操作（单字节寄存器写） |
| `I2C0_ReadByte()` | I2C 读操作（先写寄存器地址再读数据） |
| `Delay()` | 软件延时（开机动画使用） |
| `UART0_SendChar/SendString/SendBuf()` | UART 发送封装 |

**主循环调度**：
```
每 1ms:   Display_Scan()           — 数码管动态扫描（1 位/ms）
每 10ms:  Keys_Scan10ms()          — 按键消抖扫描
          Buzzer_RhythmHandler()   — 蜂鸣器节奏更新
          LED_UpdateFlashTimeout() — RX/TX 闪烁超时
          Events_SendNext()        — 事件逐条发送
每 100ms: (保留)
每 1000ms: Clock_Tick()            — 走时进位
           Alarm_Check()           — 闹钟触发检测
           LED_Heartbeat()         — D0 心跳翻转
           Events_1HzHandler()     — 1Hz DISP+LED 心跳上报
           Protocol_Process()      — 处理接收缓冲区
```

**何时修改**：添加新的主循环任务、修改开机动画、调整中断优先级。

---

#### `firmware/clock.h` + `firmware/clock.c` — 时钟模块（139 行）

**作用**：维护当前时间，支持年月日时分秒的正确进位。

**核心数据结构**：
```c
typedef struct {
    uint8_t year, month, day;       // 两位年(00-99)、月(1-12)、日
    uint8_t hour, minute, second;   // 时分秒
    uint8_t day_of_week;            // 星期(0=Sun)，Zeller 公式计算
} ClockTime;
```

**核心函数**：

| 函数 | 功能 |
|:---|:---|
| `Clock_Init()` | 初始化为默认时间（2024-06-04 12:00:00） |
| `Clock_Tick()` | 每秒+1 → 秒→分→时→日→月→年进位，含闰年/月末处理 |
| `Clock_IsLeapYear(y)` | 闰年判断（2000+y 能被 4 整除但不能被 100，除非被 400） |
| `Clock_DaysInMonth(y, m)` | 返回该月天数（28/29/30/31） |

**特性**：
- 闰年正确：2000、2004、2008…2096 为闰年，2100 不为闰年
- 月末正确：1/3/5/7/8/10/12 为 31 天，4/6/9/11 为 30 天，2 月随闰年变化
- 仅支持 2000-2099 年（year 字段为后两位）

**何时修改**：调整默认初始时间。

---

#### `firmware/alarm.h` + `firmware/alarm.c` — 闹钟模块（88 行）

**作用**：闹钟时间设定、触发检测、响铃状态管理。

**核心函数**：

| 函数 | 功能 |
|:---|:---|
| `Alarm_Init()` | 初始化为 07:00:00，未使能 |
| `Alarm_Check()` | 每秒调用：比较当前时间与闹钟时间，匹配则触发 |
| `Alarm_Stop()` | 停止响铃，关闭蜂鸣器 |
| `Alarm_IsRinging()` | 是否正在响铃（FUNC 键优先级判断用） |

**触发条件**：日(day)、时(hour)、分(minute)、秒(second) 同时相等。day=0 表示每天触发。

**响铃行为**：蜂鸣器节奏式响铃（响 200ms / 停 200ms 交替），≤10 秒自动停止。

**何时修改**：调整闹钟默认值或触发逻辑。

---

#### `firmware/display.h` + `firmware/display.c` — 显示模块（351 行）

**作用**：8 位 7 段数码管的动态扫描、数字/字母编码、流水滚动、格式控制。

**核心内容**：

| 功能 | 说明 |
|:---|:---|
| `Display_Scan()` | 每 1ms 点亮一位数码管（通过 TCA6424 I2C），8ms 完成一轮 |
| `Display_UpdateFromClock()` | 根据当前模式（TIME/DATE/YEAR）将 ClockTime 格式化为段码 |
| `Display_FillFromBuffer()` | 从显示缓冲区取 8 字符，处理小数点和 FORMAT RIGHT 逆序 |
| `Display_FlowAdvance()` | 流水模式：当内容 >8 字符时，每 N*10ms 前进一步 |
| `Display_SetMode(m)` | 切换显示模式（TIME/DATE/YEAR/FULL） |
| `Display_SetFormat(f)` | 切换流水方向（LEFT/RIGHT） |
| `Display_SetSpeed(s)` | 切换流水速度（SLOW=500ms/步, FAST=100ms/步） |

**段码表**（继承自 exp2.c）：
- `g_seg_table_num[10]`：0-9 的 7 段码（含小数点）
- `g_seg_table_alpha[26]`：A-Z 的 7 段码

**NIGHT 模式**：`g_night_mode=1` 时，仅点亮前 4 位（时分），后 4 位全灭。

**FORMAT RIGHT**：显示内容逆序排列，小数点位置按"下一位"规则调整。

**何时修改**：添加新的显示模式、修改段码表。

---

#### `firmware/keys.h` + `firmware/keys.c` — 按键模块（536 行）

**作用**：10 个按键的扫描消抖、长短按检测、编辑状态机。

**按键映射**：

| 按键 | 来源 | 短按功能 | 长按功能 |
|:---|:---|:---|:---|
| K1 FUNC | PF0 (GPIO) | 编辑模式循环：日期→时间→闹钟→退出 | 保存并退出(同SAVE) |
| K2 SHIFT | PJ0 (GPIO) | 编辑中切换字段 | — |
| K3 ADD | PJ1 (GPIO) | 当前字段 +1 | 连加 ≥5Hz |
| K4 SAVE | PN0 (GPIO) | 保存并退出编辑 | — |
| K5 DISP | TCA6424 Port0.0 | 显示切换：TIME→DATE→YEAR | — |
| K6 SPEED | TCA6424 Port0.1 | 流水速度 2 级切换 | — |
| K7 FORMAT | TCA6424 Port0.2 | 流水方向 LEFT↔RIGHT | — |
| K8 EXT | TCA6424 Port0.3 | 触发 EVT:KEY EXT (仅上报PC) | — |
| USER1 | TCA6424 Port0.4 | 触发 EVT:KEY USER1 (请求PC对时) | — |
| USER2 | TCA6424 Port0.5 | 触发 EVT:KEY USER2 (请求天气) | — |

**关键参数**：
| 参数 | 值 | 说明 |
|:---|:---|:---|
| 消抖周期 | 30ms (3 个 10ms 采样) | 状态稳定后才确认 |
| 长按阈值 | 800ms | 超过此时间触发长按事件 |
| 编辑超时 | 5000ms | 5 秒无操作自动退出且不保存 |
| 闪烁周期 | 500ms | 编辑字段 blink 指示 |

**编辑状态机**：
```
EDIT_NONE ──FUNC短按──→ EDIT_DATE ──FUNC短按──→ EDIT_TIME
    ↑                                                  │
    └──────────── FUNC短按 ←──── EDIT_ALARM ←──────────┘
    
每个编辑状态内：
  SHIFT → 切换字段    ADD → 加值    SAVE/长按FUNC → 保存退出
```

**何时修改**：调整按键功能、修改消抖/长按参数、添加新按键。

---

#### `firmware/buzzer.h` + `firmware/buzzer.c` — 蜂鸣器模块（142 行）

**作用**：闹钟响铃时控制蜂鸣器发出节奏式声音。

**核心逻辑**：
- 响铃模式：ON 200ms → OFF 200ms → ON 200ms → …（节奏式）
- 自动停止：响铃 10 秒（100 个 100ms 节拍）后自动关闭
- 硬件：蜂鸣器与 LED D7 共享 PCA9557 P07 位，`Buzzer_WriteOutput()` 同时处理两者

**关键函数**：

| 函数 | 功能 |
|:---|:---|
| `Buzzer_Start()` | 开始响铃 |
| `Buzzer_Stop()` | 停止响铃 |
| `Buzzer_RhythmHandler()` | 每 100ms 调用：控制 ON/OFF 节拍 |
| `Buzzer_WriteOutput(led_byte)` | 合并 LED 状态与蜂鸣器位写入 PCA9557 |

**何时修改**：调整响铃节奏、修改最大响铃时长。

---

#### `firmware/led.h` + `firmware/led.c` — LED 指示模块（114 行）

**作用**：8 位 LED 的状态管理、闪烁、自动超时。

**LED 位含义**：

| 位 | 名称 | 含义 | 控制方式 |
|:---|:---|:---|:---|
| D0 | HEARTBEAT | 系统心跳 | 每 1 秒自动翻转 |
| D1 | ALARM_EN | 闹钟已使能 | 跟随 `g_alarm.enabled` |
| D2 | ALARM_RING | 闹钟响铃中 | 跟随 `Alarm_IsRinging()` |
| D3 | EDIT_ACTIVE | 编辑模式 | 跟随 `Keys_GetEditState()` |
| D4 | RX_ACTIVE | 串口接收 | 收到数据亮，200ms 后自动灭 |
| D5 | TX_ACTIVE | 串口发送 | 发送数据亮，200ms 后自动灭 |
| D6 | NTP_STATUS | NTP 同步 | PC 通过 `*SET:LED` 控制（E1） |
| D7 | DAYNIGHT | 昼夜模式 | DAY=亮, NIGHT=灭 |

**重要**：PCA9557 是**低电平有效**（0=LED 亮, 1=LED 灭），代码中已处理。

**关键函数**：

| 函数 | 功能 |
|:---|:---|
| `LED_Init()` | 初始化，全灭 |
| `LED_Heartbeat()` | 每 1 秒翻转 D0 |
| `LED_RXFlash()` | D4 点亮，200ms 后自动灭 |
| `LED_TXFlash()` | D5 点亮，200ms 后自动灭 |
| `LED_UpdateFlashTimeout()` | 每 10ms 递减闪烁计数器 |
| `LED_WriteAll()` | 将当前状态写入 PCA9557 |
| `LED_SetAll(byte)` | PC 命令 `*SET:LED` 的响应函数 |

**何时修改**：调整 LED 位分配、修改闪烁时长。

---

#### `firmware/protocol.h` + `firmware/protocol.c` — 协议模块（920 行）

**作用**：实现完整的**容错三件套**（大小写不敏感、空格容错、缩写规则）和所有命令处理器。这是最大的模块。

**容错三件套**：

| 规则 | 实现 | 示例 |
|:---|:---|:---|
| 大小写不敏感 | `MatchAbbrev` 双 case 匹配 | `*set`, `*SET`, `*Set` 均合法 |
| 空格容错 | `SkipSpaces` + `NextToken` | `*SET : DATE YEAR 24` 空格任意 |
| 缩写规则 | 大写=必输、小写=可选 | `MINute` → `MIN`/`MINU`/`MINUT`/`MINUTE` 均可 |

**命令处理器**：

| 函数 | 对应命令 | 功能 |
|:---|:---|:---|
| `Cmd_RST()` | `*RST` | 软件复位 |
| `Cmd_SET_DATE(params)` | `*SET:DATE YEAR YY MONTH MM DATE DD` | 设置日期（命名参数） |
| `Cmd_SET_TIME(params)` | `*SET:TIME HOUR HH MIN MM SEC SS` | 设置时间（命名参数） |
| `Cmd_SET_ALARM(params)` | `*SET:ALARM HOUR HH MIN MM SEC SS` / `OFF` | 设置/关闭闹钟 |
| `Cmd_SET_DISPLAY(params)` | `*SET:DISP ON` / `OFF` | 显示开关 |
| `Cmd_SET_FORMAT(params)` | `*SET:FORMAT LEFT` / `RIGHT` | 流水方向 |
| `Cmd_SET_MSG(params)` | `*SET:MSG <text>` | 设置显示消息 (≤32字符) |
| `Cmd_SET_BEEP(params)` | `*SET:BEEP <ms>` | 蜂鸣 (10-5000ms) |
| `Cmd_SET_LED(params)` | `*SET:LED <hex2>` | 设置 LED 状态 |
| `Cmd_SET_KEY(params)` | `*SET:KEY <NAME>` | 模拟按键 |
| `Cmd_SET_MODE(params)` | `*SET:MODE DAY` / `NIGHT` | 昼夜模式 |
| `Cmd_PING()` | `*PING` | 回复 `*PONG <uptime_s>` |
| `Cmd_GET_DATE/TIME/ALARM/DISP/FORMAT()` | `*GET:xxx` | 查询回复 `OK <data>` |

**FORMAT RIGHT 逆序**：当格式为 RIGHT 时，`OK ` 之后的数据部分会反转。

**环形缓冲区**：
- `g_ring_buf[256]`：UART 接收中断写入，主循环读取
- 行解析：`\r`/`\n`/`\r\n` 任一视为行结束，最大帧 64 字节

**何时修改**：添加新命令、修改协议格式、调整缓冲区大小。

---

#### `firmware/events.h` + `firmware/events.c` — 事件上报模块（249 行）

**作用**：板端主动向 PC 发送事件报文，实现数字孪生的实时同步。

**事件队列**：
- 容量 16 条，循环队列
- `Events_Enqueue(event_str)` 入队，`Events_SendNext()` 逐条出队发送
- 发送限速：50ms/条（避免霸占 UART）

**关键函数**：

| 函数 | 功能 | 触发频率 |
|:---|:---|:---|
| `Events_1HzHandler()` | 每 1 秒发送 `*EVT:DISP` + `*EVT:LED` 心跳 | 1Hz |
| `Events_ReportKey(name)` | 按键事件 `*EVT:KEY <NAME>` | 即时 |
| `Events_ReportAlarm(enabled)` | 闹钟事件 `*EVT:ALARM` / `*EVT:ALARM OFF` | 即时 |
| `Events_ReportEdit(type, val)` | 编辑事件 `*EVT:EDIT <TYPE> <VALUE>` | 即时 |
| `Events_ReportDisp()` | 显示变化 `*EVT:DISP <8char> <dpHex>` | 即时 + 1Hz |
| `Events_ReportLED()` | LED 变化 `*EVT:LED <hex2>` | 即时 + 1Hz |
| `Events_ReportMode(state)` | 模式切换 `*EVT:MODE <STATE>` | 即时 |

**心跳机制**：即使无变化，`*EVT:DISP` 和 `*EVT:LED` 也每秒发送一次（全量心跳），PC 据此：
- 自动覆盖丢失的事件
- 判断连接存活（3 秒无心跳 → 超时）

**何时修改**：添加新的事件类型、调整心跳频率。

---

## 4. pc_app/ — PC 上位机（Python）

> **运行环境**：Python 3.11+ | **GUI 框架**：PyQt5（纯代码，无 .ui 文件） | **串口**：pyserial

### 4.1 模块功能速查

```
main.py (入口)
  └── MainWindow (main_window.py)
        ├── SerialWorker (QThread)   ← 串口收发线程
        ├── ProtocolParser           ← 协议解析
        ├── TwinStateManager         ← 状态镜像 + 信号发射
        ├── HeartbeatMonitor         ← 3s 超时检测
        ├── NTPClient (E1)          ← NTP 网络对时
        ├── WeatherClient (E2)      ← 天气获取 + 缓存
        └── Widgets
              ├── SevenSegWidget    ← 7 段数码管自绘
              ├── LEDBarWidget      ← 8 位 LED 指示灯
              ├── ControlPanel      ← SET/GET/演示/扩展 按钮
              └── LogPanel          ← 彩色日志 + TXT/CSV 导出
```

### 4.2 线程与信号架构

```
┌─────── UI 线程 ───────────────────────────────────────────────┐
│  MainWindow                                                    │
│    ├── ControlPanel ── send_command ──→ _on_send_command()    │
│    │        ├── ntp_sync_requested ──→ _on_ntp_sync()   [E1]  │
│    │        └── weather_fetch_requested → _on_weather_fetch()  │
│    │                                                   [E2]   │
│    ├── TwinStateManager ──→ seg/led/mode/alarm/key 信号       │
│    │        └──→ _on_seg/led/mode/key_changed() 更新 GUI      │
│    ├── HeartbeatMonitor ──→ timeout/latency 信号               │
│    └── LogPanel ←── addEntry/addError() 写入日志              │
└────────────────────────────────────────────────────────────────┘
         │                                    ▲
         │ send()                     received()
         ▼                                    │
┌─────── QThread ────────────────────────────────────────────────┐
│  SerialWorker                                                  │
│    run() → 打开 pyserial → 循环 read+write → 退出             │
│    收到的原始 bytes → received 信号 → UI 线程解析               │
└────────────────────────────────────────────────────────────────┘
```

### 4.3 每个文件详解

---

#### `pc_app/main.py` — 程序入口（133 行）

**作用**：加载 `.env` 环境变量、设置 Fusion 暗色主题、启动 QApplication。

**运行方式**：
```bash
cd 项目根目录
python -m pc_app.main
```

**主题**：Fusion 深色风格，QSS 全局样式表。

**何时修改**：修改全局主题、添加启动参数。

---

#### `pc_app/main_window.py` — 主窗口（736 行）

**作用**：GUI 布局管理、子系统信号连接、业务逻辑中枢。

**界面布局**：
```
┌────────────── 工具栏 ──────────────────────────────────┐
│ [COM口▼] [刷新] [连接] │ 状态 │ 延迟: xxms            │
├──────────── 左侧 ────────┬─────── 右侧 ────────────────┤
│   数字孪生面板            │   控制面板                   │
│   ┌─────────────────┐    │   ┌─────────────────────┐   │
│   │  7段数码管(自绘) │    │   │ 控制面板 (SET)      │   │
│   │  8位LED指示灯    │    │   │  日期/时间/闹钟/... │   │
│   │  10键模拟按钮    │    │   │  ⏱NTP 🌤天气       │   │
│   │  模式指示        │    │   │  复位 PING          │   │
│   └─────────────────┘    │   ├─────────────────────┤   │
│                          │   │ 演示 (缩写/大小写)   │   │
│                          │   │ 查询 (GET)          │   │
│                          │   └─────────────────────┘   │
├──────────────────────────┴─────────────────────────────┤
│   通信日志 (彩色 TX/RX/EVT/ERR)             [导出]     │
└────────────────────────────────────────────────────────┘
```

**核心方法**：

| 方法 | 功能 |
|:---|:---|
| `_on_send_command()` | 发送命令到串口 + TX 日志 |
| `_on_ntp_sync()` | [E1] NTP 网络请求 → 下发 DATE+TIME → D6 点灭控制 |
| `_on_weather_fetch()` | [E2] 天气 API 请求 → 缓存 → 更新时效标签 |
| `_on_key_event()` | 按键事件响应，USER2 → 自动下发天气缓存 |
| `_send_weather_to_board()` | 发送 `*SET:MSG <weather>` 到板子 |
| `_update_weather_button_state()` | 检查 API Key，无 Key 时按钮置灰 |

**何时修改**：调整界面布局、添加新信号连接、修改 NTP/天气逻辑。

---

#### `pc_app/config.py` — 配置常量（44 行）

**作用**：所有可调参数集中管理。

| 常量 | 默认值 | 说明 |
|:---|:---|:---|
| `DEFAULT_BAUD` | 115200 | 串口波特率 |
| `HEARTBEAT_TIMEOUT` | 3.0s | 心跳超时阈值 |
| `APP_NAME` / `APP_VERSION` | S800 智能联网时钟系统 / 1.0.0 | 窗口标题 |
| `COLOR_TX/RX/EVT/ERR` | 蓝/绿/橙/红 | 日志颜色 |
| `NTP_DEFAULT_SERVER` | ntp.aliyun.com | NTP 对时服务器 |
| `NTP_LED_D6_MASK` | 0x40 | 对时成功后点亮 D6 的 LED 值 |
| `NTP_LED_DURATION_MS` | 5000 | D6 点亮持续时间 |
| `WEATHER_DEFAULT_LOCATION` | Shanghai,CN | 默认城市 |
| `WEATHER_DEFAULT_CACHE_TTL` | 1800 | 天气缓存有效期 (30 分钟) |

**何时修改**：调整默认参数、修改颜色主题。

---

#### `pc_app/protocol.py` — 协议解析器（184 行）

**作用**：解析来自板端的 ASCII 帧，格式化为发送命令。与固件 `protocol.c` 配对。

**核心方法**：

| 方法 | 功能 |
|:---|:---|
| `parse_incoming(data: bytes)` | 原始 bytes → 按 CR/LF 分割 → 逐行解析 → 结构化 dict 列表 |
| `parse_response(line: str)` | 单行匹配正则 → 返回 `{type, raw, ...fields}` |
| `format_command(cmd, subcmd, params)` | 结构化参数 → `*CMD:SUBCMD param1 param2` |
| `validate_abbreviation(input, full)` | 验证缩写规则（大写必输/小写可选） |
| `is_heartbeat_frame(frame)` | 判断是否为 DISP/LED 心跳帧 |

**支持的事件类型解析**：evt_key, evt_alarm, evt_edit, evt_disp, evt_led, evt_mode, pong, ok, error

**何时修改**：协议新增事件类型、修改帧格式。

---

#### `pc_app/serial_worker.py` — 后台串口线程（182 行）

**作用**：独占串口的 QThread，收发分离、互斥锁保护发送缓冲。

**关键信号**：
| 信号 | 含义 |
|:---|:---|
| `connected(bool, str)` | 连接成功/失败 |
| `received(bytes)` | 收到原始数据 |
| `error(str)` | 串口错误 |

**何时修改**：修改串口参数。

---

#### `pc_app/twin_state.py` — 数字孪生状态镜像（135 行）

**作用**：维护板端状态镜像（SEG/LED/MODE/FORMAT/ALARM），状态变化时发射 Qt 信号，GUI 控件通过连接信号自动更新。这是 PC 端数字孪生的核心。

**关键信号**：
| 信号 | 参数 | 触发条件 |
|:---|:---|:---|
| `seg_changed` | (text: str, dp_hex: int) | 收到 EVT:DISP |
| `led_changed` | (byte: int) | 收到 EVT:LED |
| `mode_changed` | (mode: str) | 收到 EVT:MODE |
| `key_event` | (name: str) | 收到 EVT:KEY |
| `alarm_changed` | (enabled: bool) | 收到 EVT:ALARM |

**何时修改**：增加新的镜像状态、添加新信号。

---

#### `pc_app/heartbeat.py` — 心跳超时监控（102 行）

**作用**：500ms 定时器检查，3 秒无心跳 → `timeout` 信号 → 状态栏变红。

**心跳源**：`*EVT:DISP` 和 `*EVT:LED`（板端每秒自动发送）

**PING/PONG 延迟**：记录 PING 发送时间戳，收到 PONG 时计算 RTT。

**何时修改**：调整超时阈值、心跳检查频率。

---

#### `pc_app/ntp_client.py` — NTP 时间同步客户端（146 行）[E1]

**作用**：从互联网 NTP 服务器获取精确时间，转换为本地时区，生成 `*SET:DATE` 和 `*SET:TIME` 命令字符串。

**核心类**：
| 类 | 功能 |
|:---|:---|
| `NTPResult` | 数据类：success, year/month/day/hour/minute/second, error_msg |
| `NTPClient` | 封装 ntplib，从 `.env` 读取服务器地址和超时 |

**何时修改**：更换默认 NTP 服务器。

---

#### `pc_app/weather_client.py` — 天气 API 客户端（192 行）[E2]

**作用**：从 OpenWeatherMap 或和风天气获取实时天气，格式化为 ≤8 字符的显示字符串（如 `Sunny26C`），30 分钟缓存。

**核心类**：
| 类 | 功能 |
|:---|:---|
| `WeatherResult` | 数据类：success, display_text(≤8字符), error_msg, timestamp |
| `WeatherCache` | 缓存管理：is_valid, age_seconds, age_text("12分钟前") |
| `WeatherClient` | API 调用 + 解析 + 缓存 |

**支持 API**：OpenWeatherMap（默认）、和风天气（设置 `WEATHER_API_TYPE=hefeng`）

**何时修改**：添加新的天气 API 支持、修改默认城市。

---

#### `pc_app/widgets/seven_seg.py` — 7 段数码管自绘（265 行）

**作用**：QPainter 自绘 8 位 7 段数码管，梯形多边形段（a-g），红色 LED 风格，支持 NIGHT 模式。

**何时修改**：调整颜色、修改段形状。

---

#### `pc_app/widgets/led_indicator.py` — LED 指示灯（188 行）

**作用**：圆形 LED（带辉光+高光效果）+ LEDBarWidget（8 位 LED 条 + 标签）。

**何时修改**：调整 LED 外观。

---

#### `pc_app/widgets/log_panel.py` — 通信日志面板（211 行）

**作用**：QPlainTextEdit 彩色日志，方向编码（TX 蓝/RX 绿/EVT 橙/ERR 红），支持导出 TXT/CSV。

**何时修改**：调整日志格式、导出功能。

---

#### `pc_app/widgets/control_panel.py` — 命令控制面板（458 行）

**作用**：三组控制 + 两个扩展按钮：

| 分组 | 控件 | 功能 |
|:---|:---|:---|
| 控制面板 | 日期预设/时间/闹钟/显示/格式/消息/蜂鸣/LED/模式/复位/PING | 所有 SET/GET 命令 |
| 扩展 | ⏱ NTP 对时 / 🌤 获取天气 | E1/E2 |
| 演示 | 缩写命令演示 / 大小写混合演示 | 容错三件套演示 |
| 查询 | 获取日期/时间/闹钟/显示/格式 | GET 命令 |

**关键信号**：
| 信号 | 含义 |
|:---|:---|
| `send_command(str)` | 发送协议命令 |
| `ntp_sync_requested()` | NTP 对时按钮 |
| `weather_fetch_requested()` | 天气获取按钮 |

**何时修改**：添加新控制按钮、修改参数范围。

---

## 5. driverlib/ 和 inc/ — TivaWare 驱动库

> **来源**：Texas Instruments TivaWare for C Series (SW-TM4C-DRL-UG-2.1.4.178)
>
> **作用**：提供 TM4C129 所有外设的硬件抽象层 API 和寄存器定义。
>
> **何时需要**：编译固件时作为依赖。这两个目录**不需要修改**。

### driverlib/ — 外设驱动库（约 60 个 .c/.h 文件）

包含 GPIO、I2C、UART、SysTick、SysCtl、Interrupt 等所有外设的驱动函数。编译时只需要把用到的 .c 文件（gpio.c, i2c.c, uart.c, sysctl.c, systick.c, interrupt.c）加入 Keil 工程。

预编译库 `driverlib/rvmdk/driverlib.lib` 可以代替源码编译（链接更快），二者选其一即可。

### inc/ — 硬件寄存器定义头文件（33 个 .h 文件）

每个外设的寄存器地址、位掩码定义。例如 `hw_gpio.h` 定义了 GPIO 端口基址和数据寄存器的偏移量。通过 `#include "hw_memmap.h"` 等引入。

---

## 6. RTE/ — Keil 运行时环境

> **来源**：Keil MDK 自动生成
>
> **作用**：提供芯片启动代码和系统初始化。
>
> **何时需要**：编译固件时必需。

| 文件 | 作用 | 是否可改 |
|:---|:---|:---|
| `Device/TM4C1294NCPDT/startup_TM4C129.s` | 汇编启动文件：堆栈初始化、中断向量表、Reset_Handler | 不可改 |
| `Device/TM4C1294NCPDT/system_TM4C129.c` | C 系统初始化：`SystemInit()` 设置时钟 | 不可改 |
| `_Target_1/RTE_Components.h` | RTE 组件配置头文件 | 通过 Keil RTE 管理器修改 |

**Objects/** 和 **Listings/** 是编译时自动生成的中间产物，不需要手动管理（已在 `.gitignore` 中排除）。

---

## 7. 快速上手

### 7.1 编译烧录固件

```
1. 用 USB 线连接 S800 板子到电脑

2. 打开 Keil MDK → Project → Open → 选择 S524031910727.uvprojx

3. 确认 Project 窗口中已添加以下 .c 文件：
   firmware/     → 全部 9 个 .c 文件 (main, clock, alarm, display, keys,
                                    buzzer, led, protocol, events)
   driverlib/    → gpio.c, i2c.c, uart.c, sysctl.c, systick.c, interrupt.c
   RTE/Device/   → system_TM4C129.c, startup_TM4C129.s

4. Options → C/C++ → Include Paths:
     .\inc
     .\driverlib

5. F7 编译 → 确认 0 错误 0 警告 → F8 烧录

6. 烧录完毕板子自动复位，数码管显示开机动画
```

### 7.2 运行 PC 上位机

```bash
# 1. 首次：安装依赖
pip install -r requirements.txt

# 2. （可选）配置环境变量
copy .env.example .env
# 编辑 .env，填入天气 API Key 等

# 3. 启动上位机
python -m pc_app.main
```

```
4. GUI 启动后：
   - 顶部下拉选择 COM 口 → 点「连接」
   - 连接成功 → 数字孪生面板开始同步
   - 所有控制按钮可用
```

### 7.3 板端独立使用

板子烧录固件后**可以脱离 PC 独立运行**，就像一个普通电子钟：

- 数码管显示当前时间（HH.MM.SS）
- K5 (DISP) 切换显示：时间 → 日期 → 年份
- K1 (FUNC) 进入编辑模式 → K2/K3/K4 编辑日期/时间/闹钟
- 闹钟到点自动响铃，按 K1 停止
- 接上 USB 连 PC 后自动开始上报事件，无缝切换为双端协作模式

---

## 8. 功能使用指南

### 8.1 板端按键操作

#### 编辑日期/时间/闹钟

```
1. 短按 FUNC (K1) → 进入日期编辑（第一个字段闪烁）
2. 按 SHIFT (K2) → 切换到下一字段
3. 按 ADD (K3) → 当前字段 +1（长按连加 ≥5Hz）
4. 按 SAVE (K4) → 保存并退出编辑
   或 长按 FUNC → 保存并退出
5. 5 秒不操作 → 自动退出（不保存）

再按 FUNC → 进入时间编辑
再按 FUNC → 进入闹钟编辑
再按 FUNC → 退出编辑模式
```

#### 显示切换

```
按 DISP (K5) → 循环切换：
  HH.MM.SS (时间) → YY.MM.DD (日期) → YYYYMMDD (年份)

按 SPEED (K6) → 流水速度 慢/快 切换
按 FORMAT (K7) → 流水方向 左对齐/右对齐 切换
```

#### 停止闹钟

```
闹钟响铃中 → 按 FUNC (K1) → 立即停止
（FUNC 优先级最高，不进入编辑模式）
```

### 8.2 PC 上位机操作

#### 连接板子

```
1. 确保板子通过 USB 线连接
2. 在工具栏 COM 口下拉中选择正确的端口
3. 点「连接」按钮
4. 状态栏显示 "已连接: COMx | 格式:LEFT | 闹钟OFF" 即成功
```

#### 发送命令

```
控制面板 →
  日期：选择预设或手动设置年月日 → 点「设置日期」
  时间：调整时分秒 → 点「设置时间」
  闹钟：设置闹钟时间 → 点「设置闹钟」或「关闭闹钟」
  显示：点「开启」/「关闭」
  格式：点「左对齐」/「右对齐」
  消息：输入文本(≤32字符) → 点「发送消息」
  蜂鸣：设置时长 → 点「蜂鸣」
  LED：输入十六进制值(00-FF) → 点「设置 LED」
  模式：点「白天模式」/「夜间模式」

查询 →
  点「获取日期/时间/闹钟/显示/格式」→ 日志区显示回复

演示 →
  「缩写命令演示」→ 发送 *SET:TIME ... MIN ... SEC（验证缩写容错）
  「大小写混合演示」→ 发送 *SeT:TiMe ... MiNuTe ... SeCoNd（验证大小写容错）
```

#### 数字孪生面板

```
7 段数码管：实时镜像板端显示内容
LED 指示灯：实时镜像板端 8 位 LED 状态
按键模拟：点击 K1-K8/U1/U2 → 等效于在板子上按对应键
```

#### 通信日志

```
TX（蓝色）：PC → 板子发送的命令
RX（绿色）：板子 → PC 返回的响应
EVT（橙色）：板子主动上报的事件
ERR（红色）：错误信息

右键菜单 → 导出 TXT / 导出 CSV
```

### 8.3 串口通信协议

#### 协议基础

| 参数 | 值 |
|:---|:---|
| 物理层 | UART0 (PA0/PA1), 115200, 8N1, 无流控 |
| 编码 | ASCII |
| 行结束符 | `\r`、`\n`、`\r\n` 均可 |
| 最大帧长 | 64 字节 |

#### 命令格式

```
通用格式：*<CMD>:<SUBCMD> <param1> <param2> ...

规则：
  1. 大小写不敏感: *set, *SET, *Set 均合法
  2. 空格容错: 多个空格/Tab 等效于单个空格
  3. 缩写规则: 大写字母必输，小写字母可选
     例: MINute → MIN, MINU, MINUT, MINUTE 均可
```

#### 命令速查表

| 命令 | 格式 | 应答 | 说明 |
|:---|:---|:---|:---|
| 复位 | `*RST` | OK | 软件复位板子 |
| 设置日期 | `*SET:DATE YEAR YY MONTH MM DATE DD` | OK/ERROR | 命名参数，单参或组合 |
| 设置时间 | `*SET:TIME HOUR HH MIN MM SEC SS` | OK/ERROR | 缩写: MIN/MINute, SEC/SECond |
| 设置闹钟 | `*SET:ALARM HOUR HH MIN MM SEC SS` | OK/ERROR | OFF 关闭 |
| 显示开关 | `*SET:DISP ON` / `OFF` | OK/ERROR | ON/OFF 不能缩写（全大写） |
| 流水方向 | `*SET:FORMAT LEFT` / `RIGHT` | OK/ERROR | LEFT/RIGHT 全大写 |
| 设置消息 | `*SET:MSG <text>` | OK/ERROR | ≤32 ASCII 字符 |
| 蜂鸣 | `*SET:BEEP <ms>` | OK/ERROR | 10-5000 ms |
| 设置 LED | `*SET:LED <hex2>` | OK/ERROR | 如 40=点亮D6, 00=全灭 |
| 模拟按键 | `*SET:KEY <NAME>` | OK | 如 FUNC, SHIFT, ADD, USER1 |
| 昼夜模式 | `*SET:MODE DAY` / `NIGHT` | OK/ERROR | NIGHT 仅显示时分的后4位 |
| 查询 | `*GET:DATE` / `TIME` / `ALARM` / `DISP` / `FORMAT` | OK <data> | 返回当前值 |
| 心跳 | `*PING` | `*PONG <uptime_s>` | 测量延迟 + 验证连接 |

#### 事件报文（板端 → PC）

| 报文 | 触发条件 |
|:---|:---|
| `*EVT:KEY <NAME>` | 物理按键按下 |
| `*EVT:ALARM` / `*EVT:ALARM OFF` | 闹钟开始/停止 |
| `*EVT:EDIT <TYPE> <VALUE>` | 板端本地编辑保存 |
| `*EVT:DISP <8字符> <dpHex>` | 显示变化 + 每秒心跳 |
| `*EVT:LED <hex2>` | LED 变化 + 每秒心跳 |
| `*EVT:MODE <STATE>` | 模式切换（DAY/NIGHT） |

### 8.4 E1: NTP 一键对时

**前置条件**：
- `.env` 文件存在（或使用默认配置）
- `NTP_SERVER` 指向可访问的 NTP 服务器（默认 `ntp.aliyun.com`）
- 电脑能访问互联网
- 串口已连接

**使用步骤**：
```
1. 连接板子
2. 点「⏱ NTP 对时」按钮
3. 状态栏显示 "NTP 对时中..."
4. 成功后：
   - 状态栏显示 "NTP ✓" (2秒)
   - 板端时间被设置为互联网精确时间
   - LED D6 点亮 5 秒后自动熄灭
5. 失败：
   - 弹窗提示错误原因
   - 日志区标红 ERROR 记录
```

**流程细节**：
```
PC 端                          板端
  │                              │
  ├─ NTP 请求 ntp.aliyun.com ───│
  ├─ 获取 UTC，转本地时区        │
  ├─→ *SET:DATE YEAR YY ... ──→ │ 设置日期
  ├─ ←──────────── OK ────────┤
  ├─→ *SET:TIME HOUR HH ... ──→ │ 设置时间
  ├─ ←──────────── OK ────────┤
  ├─→ *SET:LED 40 ────────────→ │ 点亮 D6
  │       ... 5 秒 ...          │
  ├─→ *SET:LED 00 ────────────→ │ 熄灭 D6
```

### 8.5 E2: 天气获取与短显

**前置条件**：
- `.env` 文件存在且 `WEATHER_API_KEY` 已配置
- 电脑能访问互联网
- 串口已连接

**获取天气**：
```
1. 点「🌤 获取天气」按钮
2. 状态栏显示 "获取天气中..."
3. 成功后：
   - 日志显示 "天气获取成功: Sunny26C"
   - 缓存标签显示 "刚刚"
4. 失败：
   - 弹窗提示错误
   - 旧缓存（如有）保留并标记"(过期)"
```

**在板子上查看天气**：
```
1. 按板子上的 USER2 键
2. 板子发送 *EVT:KEY USER2
3. PC 自动回复 *SET:MSG <天气字符串>
4. 板子数码管流水显示天气（如 "Sunny26C"），5 秒后自动恢复时钟
```

**API Key 缺失时**：
- 🌤 按钮置灰
- 鼠标悬停 Tooltip 显示："未配置 WEATHER_API_KEY，请在 .env 中设置后重启程序"

---

## 9. 环境变量配置

复制 `.env.example` 为 `.env` 并编辑：

```ini
# ── 串口（可选，通常自动检测）──
# DEFAULT_PORT=COM3
# DEFAULT_BAUD=115200

# ── E1: NTP 对时 ──
NTP_SERVER=ntp.aliyun.com      # NTP 服务器地址
NTP_TIMEOUT=3                  # 请求超时秒数

# ── E2: 天气 API ──
WEATHER_API_TYPE=openweathermap  # 或 hefeng
WEATHER_API_KEY=你的_API_Key     # 【必需】注册获取: https://openweathermap.org/api
WEATHER_LOCATION=Shanghai,CN     # 城市名(OpenWeatherMap) 或 城市ID(和风)
WEATHER_CACHE_TTL=1800           # 缓存有效期(秒)，默认 30 分钟
```

**获取免费 API Key**：
- OpenWeatherMap: 注册 https://openweathermap.org → API Keys → 免费 tier（60次/分钟）
- 和风天气: 注册 https://dev.qweather.com/ → 控制台 → 免费订阅（1000次/天）

---

## 10. 故障排查

### 编译问题

| 现象 | 原因 | 解决 |
|:---|:---|:---|
| `error: #5: cannot open source file` | Include Paths 未配 | Options → C/C++ → Include Paths 添加 `.\inc` 和 `.\driverlib` |
| `error: #20: identifier "xxx" is undefined` | 缺少 .c 文件 | 确认 firmware/ 下所有 9 个 .c 已加入工程 |
| `Undefined symbol xxx (referred from main.o)` | 链接缺文件 | 确认 driverlib/ 的 .c 文件已加入工程 |
| 大量 linker 错误 | startup 文件缺失 | 确认 `startup_TM4C129.s` 在工程中 |

### 串口连接问题

| 现象 | 原因 | 解决 |
|:---|:---|:---|
| COM 口列表中无端口 | 驱动未安装 | 安装 Stellaris ICDI 驱动或 CP210x 驱动 |
| 连接后无心跳 | 波特率不匹配 | 确认固件和上位机都是 115200 |
| 连接后无心跳 | 板子未烧录或卡死 | 重新烧录固件、检查 USB 线 |
| 3 秒后断开（超时） | USB 接触不良 | 换 USB 口或线 |
| 收到乱码 | 波特率不对 | 确认上位机和固件都是 115200 |

### Python 环境问题

| 现象 | 原因 | 解决 |
|:---|:---|:---|
| `No module named 'PyQt5'` | 未安装依赖 | `pip install -r requirements.txt` |
| `No module named 'ntplib'` | E1/E2 新增依赖 | `pip install ntplib requests` |
| 启动闪退 | `.env` 编码问题 | 确保 `.env` 为 UTF-8 编码 |
| Qt 平台插件错误 | Qt DLL 冲突 | `pip uninstall PyQt5 && pip install PyQt5` |

### 板端功能问题

| 现象 | 原因 | 解决 |
|:---|:---|:---|
| 数码管不亮 | NIGHT 模式 | PC 发送 `*SET:MODE DAY` 或复位板子 |
| 按键无反应 | 编辑超时 | 5 秒无操作自动退出编辑，正常 |
| 蜂鸣器不响 | 闹钟未使能 | 先设置闹钟，确认 LED D1 亮 |
| 时间不走 | SysTick 异常 | 复位板子或重新烧录 |
| LED 行为异常 | PCA9557 地址不对 | 确认 I2C 地址为 0x18（非 PRD 中的 0x38） |

---

> **最后更新**：2026-06-04
> **版本**：v1.0.0（含 E1 NTP 对时 + E2 天气扩展）
