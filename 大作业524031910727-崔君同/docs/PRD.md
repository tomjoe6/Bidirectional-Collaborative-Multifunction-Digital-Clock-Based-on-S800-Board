# S800 智能联网时钟系统 · 板端+PC上位机联合开发 PRD
>始终遵循少冗余、模块化、不确定处及时报告、尽量省token的原则
## 1. 项目概述

| 项 | 说明 |
| :--- | :--- |
| **项目名称** | 智能联网时钟系统（S800 + PC 数字孪生） |
| **功能基准** | 《大作业题目-学生版_V1.2》全文 |
| **硬件/端口基准** | exp2.c 中已验证的 GPIO/I2C/UART/SysTick 底层定义 |
| **核心目标** | S800 本地独立运行时钟/闹钟/编辑；PC 通过 USB 虚拟串口实现远程控制、状态监视、1:1 数字孪生镜像双向同步 |
| **技术栈** | 板端: TM4C129 + DriverLib + I2C (TCA6424+PCA9557) + UART0<br>PC端: Python 3.11 + PyQt5 + pyserial + python-dotenv |


## 2. 板端硬件与端口定义（继承 exp2.c）

>我之前在exp2.c中是使用USRSW1和USRSW2来切换功能的，此次实验除了这两个，板子上还有8个SW按键可用于下列其他功能，一共十个

### 2.1 外设初始化参数

| 外设 | exp2.c 定义 | 备注 |
| :--- | :--- | :--- |
| UART0 | PA0(RX), PA1(TX), 115200, 8N1 | 中断接收，FIFO 使能 |
| I2C0 | PB2(SCL), PB3(SDA), 400kHz | TCA6424(0x44) + PCA9557(0x38) |
| SysTick | 1ms 时基 | 用于走时、按键消抖、流水定时 |
| GPIO 按键 | PF0(K1), PJ0(K2), PJ1(K3), PN0(K4) | 上拉输入，边沿触发中断 |
| GPIO 扩展键 | K5-K8 对应 exp2.c 中 DISP/SPEED/FORMAT/EXT 引脚 | 同上 |
| USER1/USER2 | exp2.c 中预留的外部 GPIO 输入 | 用于请求对时 / 天气显示 |
| 蜂鸣器 | exp2.c 中 PWM 输出引脚 | 闹钟响铃使用 |

### 2.2 数码管与 LED 驱动

- 段码表、位选表、小数点位掩码完全复用 `exp2.c` 中的 `g_seg_table[]`、`g_digit_sel[]`、`g_dp_mask`
- 动态扫描周期、亮度等级沿用 exp2.c 已调优参数
- NIGHT 模式仅点亮时分4位 + LED仅保留心跳位，需在原扫描函数中增加条件分支

## 3. 板端功能规格（V1.2 §3 全部必做）

### 3.1 开机画面

1. 8位数码管 + 8位LED 全亮→全灭，闪烁 ≥1 次
2. 显示学号后8位并闪烁1次，LED同步闪烁
3. 显示姓名拼音（≤8字符）并闪烁1次
4. 显示软件版本号 ≥1秒 → 进入正常时钟显示

### 3.2 时钟与日期显示

- 默认 HH.MM.SS，按键切换 YY.MM.DD / YYYY.MMDD
- 闰年、月末进位（28/29/30/31）正确
- 走时基准来自 1ms SysTick，秒不丢不抖
- 接收 `*SET:DISP OFF` 整屏熄灭，`ON` 恢复

### 3.3 流水显示

- 内容 >8 位时按固定速率流水
- 方向由 `*SET:FORMAT LEFT/RIGHT` 或本地 FORMAT 键控制
- 速度 2 级可调（SPEED 键切换）
- **小数点随方向跟随**：左→右用本位小数点，右→左用下一位小数点

### 3.4 闹钟功能

- 日时分秒同时相等触发；蜂鸣器节奏式响铃（响-停-响），持续 ≤10秒自动停止
- 响铃中按 FUNC 立即停止（FUNC 优先于编辑模式切换）
- LED 指示闹钟使能/响铃状态

### 3.5 按键设置（编辑状态机）

| 按键 | 短按功能 | 长按功能 |
| :--- | :--- | :--- |
| FUNC (K1) | 编辑模式循环：日期→时间→闹钟→退出（响铃中关闹钟） | 保存并退出（同SAVE） |
| SHIFT (K2) | 编辑中切换高亮字段 | — |
| ADD (K3) | 当前字段+1 | 连加（≥5Hz） |
| SAVE (K4) | 保存并退出编辑 | — |
| DISP (K5) | 主显示切换：时间/日期/年份 | — |
| SPEED (K6) | 流水速度2级切换 | — |
| FORMAT (K7) | 流水方向切换（等价 *SET:FORMAT） | — |
| EXT (K8) | 触发 *EVT:KEY EXT | — |
| USER1 (GPIO) | 请求PC对时（触发 *EVT:KEY USER1） | — |
| USER2 (GPIO) | 数码管短显天气5秒（需PC先下发） | — |

- 5秒无操作自动退出且不保存
- 1秒内连续按 ≥3 次不丢键

### 3.6 LED 辅助指示（8位全部使用）

| LED位 | 含义 |
| :--- | :--- |
| D0 | 系统心跳（1Hz翻转） |
| D1 | 闹钟使能 |
| D2 | 闹钟响铃中 |
| D3 | 编辑模式激活 |
| D4 | 串口接收活动 |
| D5 | 串口发送活动 |
| D6 | NTP同步状态（扩展E1） |
| D7 | 昼夜模式指示（DAY=亮/NIGHT=灭） |

## 4. 串口通信协议（V1.2 §5 + exp2.c 物理层）

### 4.1 物理层（继承 exp2.c）

| 参数 | 值 |
| :--- | :--- |
| 端口 | UART0 (PA0/PA1) |
| 波特率 | 115200, 8N1, 无流控 |
| 编码 | ASCII |
| 行结束符 | `\r` / `\n` / `\r\n` 任一（替换 exp2.c 中的 `#` 终止符） |
| 单帧最大长度 | 64字节 |
| 接收方式 | UART0 中断 + 环形缓冲区 + 行解析状态机 |

### 4.2 容错三件套（必须实现）

1. **大小写不敏感**：命令、子命令、参数字面量均合法
2. **空格容错**：命令与参数、参数间允许 1个或多个空格/Tab
3. **缩写规则**：大写字母为必输部分，小写可省略（如 MINute → MIN/MINU/MINUT/MINUTE）

### 4.3 PC→S800 命令总表

| 命令 | 子命令 | 参数 | 应答 |
| :--- | :--- | :--- | :--- |
| *RST | — | — | OK |
| *SET | :DATE | YEAR/MONTH/DATE（单参或组合） | OK / ERROR |
| *SET | :TIME | HOUR/MINute/SECond（单参或组合） | OK / ERROR |
| *SET | :ALARM | HOUR/MINute/SECond/OFF | OK / ERROR |
| *SET | :DISPlay | ON/OFF | OK / ERROR |
| *SET | :FORMAT | LEFT/RIGHT | OK / ERROR |
| *SET | :MSG | <text> ≤32字节 | OK / ERROR |
| *SET | :BEEP | <ms> 10-5000 | OK / ERROR |
| *SET | :LED | <hex2> | OK / ERROR |
| *SET | :KEY | <NAME>（10项见§3.5） | OK（不再上报*EVT:KEY） |
| *SET | :MODE | DAY/NIGHT | OK / ERROR |
| *GET | :DATE/:TIME/:ALARM/:DISP/:FORMAT | 同SET参数或省略 | OK <data> |
| *PING | — | — | *PONG <uptime_s> |

### 4.4 S800→PC 主动事件报文

| 报文 | 触发条件 | 频率 |
| :--- | :--- | :--- |
| *EVT:KEY <NAME> | 物理按键按下 | 即时 |
| *EVT:ALARM / *EVT:ALARM OFF | 闹钟开始/停止响铃 | 即时 |
| *EVT:EDIT <TYPE> <VALUE> | 本地编辑保存 | 即时 |
| *EVT:DISP <8字符> <dpHex> | 显示变化 | 即时 + **1Hz心跳** |
| *EVT:LED <hex2> | LED变化 | 即时 + **1Hz心跳** |
| *EVT:MODE <STATE> | 模式切换 | 即时 |
| *PONG <uptime_s> | 应答 *PING | 即时 |

> 💡 **关键机制**：*EVT:DISP 和 *EVT:LED 每秒发送一次（即使无变化），作为 1Hz 全量心跳，PC 据此自动覆盖丢失事件，无需重传。

### 4.5 FORMAT RIGHT 行为

当 FORMAT=RIGHT 时，所有应答与数码管显示**逆序**，小数点位置按"下一位"规则跟随。
例：时间 12:30:45 → LEFT 应答 `OK 12.30.45`，RIGHT 应答 `OK 54.03.21`

## 5. PC 上位机功能规格（V1.2 §4）

### 5.1 GUI 控件清单

#### 5.1.1 顶部工具栏

| 控件 | 对象名 | 功能 |
| :--- | :--- | :--- |
| QComboBox | combo_port | 自动扫描COM端口 |
| QPushButton | btn_connect | 连接/断开，状态联动 |
| QLabel | lbl_status | 连接状态 + FORMAT + MODE + ALARM使能 四项实时显示 |
| QLabel | lbl_latency | 心跳延迟显示 |

#### 5.1.2 控制面板区

| 控件 | 对象名 | 功能 |
| :--- | :--- | :--- |
| QGroupBox | grp_set | 包含所有 *SET 命令的下拉/输入/按钮 |
| QComboBox | cmb_param_combo | ≥3种参数组合预设下拉 |
| QPushButton | btn_abbrev_demo | 缩写演示按钮 |
| QPushButton | btn_case_demo | 大小写混合演示按钮 |
| QPushButton | btn_ntp_sync | NTP一键对时（扩展E1） |
| QPushButton | btn_weather | 天气获取下发（扩展E2） |

#### 5.1.3 数字孪生镜像面板（核心 P3）

| 组件 | 规格 | 同步机制 |
| :--- | :--- | :--- |
| 8位7SEG | 自绘Widget，支持段码+小数点渲染 | 监听 *EVT:DISP <8字符><dpHex> |
| 8位LED | 圆形指示灯，逐位解码 | 监听 *EVT:LED <hex2> |
| 8位按键 | K1-K8 可点击按钮 | 点击 → 下发 *SET:KEY <NAME> |
| USER1/USER2 | 独立可点击控件 | 点击 → 下发 *SET:KEY USER1/USER2 |
| NIGHT模式 | 仅点亮时分4位 + LED仅保留心跳 | 监听 *EVT:MODE NIGHT 自动切换 |

#### 5.1.4 收发日志区

| 功能 | 要求 |
| :--- | :--- |
| 时间戳 | [HH:MM:SS.mmm] |
| 方向标记 | TX/RX/EVT/ERR 颜色编码 |
| 导出 | 支持导出为 txt/csv |
| 异常高亮 | ERROR 应答红色高亮 + 弹窗提示 |

### 5.2 后台线程架构

```text
MainWindow (UI Thread)
    ├── SerialWorker (QThread) ← 独占串口，信号槽通信
    │     ├── signal_send(bytes)
    │     ├── signal_received(bytes)
    │     ├── signal_connected(bool, str)
    │     └── signal_error(str)
    ├── ProtocolParser ← 大小写/空格/缩写容错解析
    ├── TwinStateManager ← 维护 SEG/LED/MODE 镜像状态
    └── HeartbeatMonitor ← 检测 *EVT:DISP/LED 1Hz心跳超时