# S800 智能联网时钟系统 — 板端+PC上位机

基于 TM4C129 (S800 Board) 的双向协作多功能数字时钟，支持板端独立运行及 PC 端 USB 虚拟串口远程控制与数字孪生同步。

项目源自上海交通大学"嵌入式系统与接口技术"课程。

## 项目结构

```
├── PRD.md                          # 产品需求规格文档
├── PROJECT.md                      # 完整项目文档（中文，强烈推荐阅读）
├── firmware/                       # 板端固件（C 语言 / TivaWare DriverLib）
│   ├── hw_config.h                 # 硬件引脚/I2C/UART/SysTick 定义
│   ├── clock.h / clock.c           # 时钟状态管理（闰年/月末进位）
│   ├── alarm.h / alarm.c           # 闹钟触发检测
│   ├── display.h / display.c       # 7段数码管动态扫描 + 流水显示
│   ├── keys.h / keys.c             # 10 键扫描 + 编辑状态机
│   ├── buzzer.h / buzzer.c         # 蜂鸣器节奏控制
│   ├── led.h / led.c               # 8 位 LED 指示灯管理
│   ├── protocol.h / protocol.c     # 完整串口协议解析器（容错三件套）
│   ├── events.h / events.c         # 主动事件上报（1Hz 心跳）
│   └── main.c                      # 主程序（开机动画 + 主循环 + ISR）
│
├── pc_app/                         # PC 上位机（Python 3.11 + PyQt5）
│   ├── main.py                     # 程序入口 + 暗色主题
│   ├── main_window.py              # 主窗口布局与信号连接
│   ├── config.py                   # 配置常量
│   ├── protocol.py                 # 协议解析器（大小写/空格/缩写容错）
│   ├── serial_worker.py            # 后台串口线程（QThread）
│   ├── twin_state.py               # 数字孪生状态镜像
│   ├── heartbeat.py                # 心跳超时监控
│   ├── ntp_client.py               # [E1] NTP 时间同步客户端
│   ├── weather_client.py           # [E2] 天气 API 客户端
│   └── widgets/
│       ├── seven_seg.py            # 自绘 7 段数码管组件
│       ├── led_indicator.py        # LED 指示灯组件
│       ├── log_panel.py            # 彩色收发日志面板
│       └── control_panel.py        # 命令控制面板（SET/GET/演示）
│
├── requirements.txt                # PC 端 Python 依赖
├── .env.example                    # 环境变量示例
└── README.md                       # 本文件
```

## 板端固件

### 硬件需求
- TM4C1294NCPDT (S800 Board)
- TCA6424 I2C GPIO 扩展器 (0x22)
- PCA9557 I2C GPIO 扩展器 (0x18)
- 8 位共阴数码管
- 8 个扩展按键 (K1-K8 + USER1/USER2)
- 蜂鸣器 (PCA9557 P07)
- USB 转串口 (UART0, PA0/PA1)

### 编译
1. 使用 Keil MDK / IAR / CCS 打开工程
2. 确保 `firmware/` 下所有 `.c` 文件已添加到工程
3. 包含路径需覆盖 `driverlib/`、`inc/`、`RTE/` 目录
4. 编译目标：TM4C1294NCPDT

### 功能
- **开机画面**：全亮闪烁 → 学号 → 姓名拼音 → 版本号
- **时钟/日期**：HH.MM.SS / YY.MM.DD / YYYYMMDD，闰年正确
- **流水显示**：>8 字符自动滚动，方向/速度可调
- **闹钟**：日时分秒匹配触发，蜂鸣器节奏响铃，≤10 秒自动停止
- **按键编辑**：10 键，短按/长按/连加，5 秒无操作自动退出
- **LED 指示**：心跳/闹钟使能/响铃/编辑/RX/TX/NTP/昼夜
- **NIGHT 模式**：仅点亮时分 4 位 + 心跳 LED
- **串口协议**：完整 *RST/*SET/*GET/*PING 命令集 + 容错三件套

## PC 上位机

### 安装依赖

```bash
# 激活虚拟环境
.venv\Scripts\activate

# 安装依赖
pip install -r requirements.txt
```

### 运行

```bash
cd pc_app 的父目录
python -m pc_app.main
```

### 界面功能

| 区域 | 功能 |
|:---|:---|
| **顶部工具栏** | COM 端口选择、连接/断开、状态显示、延迟显示 |
| **数字孪生面板** | 实时 7 段数码管镜像、8 位 LED 指示灯、10 键模拟按钮 |
| **控制面板** | *SET/*GET 全部命令、日期预设、蜂鸣控制、模式切换 |
| **演示按钮** | 缩写命令演示（MIN/SEC）、大小写混合演示 |
| **通信日志** | 带时间戳的 TX/RX/EVT/ERR 彩色日志，支持 TXT/CSV 导出 |

### 后台架构

```
MainWindow (UI Thread)
  ├── SerialWorker (QThread)  ← 独占串口，信号槽通信
  ├── ProtocolParser          ← 大小写/空格/缩写容错解析
  ├── TwinStateManager        ← 维护 SEG/LED/MODE 镜像状态
  └── HeartbeatMonitor        ← 检测 1Hz 心跳超时 (3s)
```

## 通信协议

### 物理层
- UART0: PA0(RX), PA1(TX), 115200, 8N1, 无流控
- ASCII 编码，行结束符 `\r` / `\n` / `\r\n` 均可
- 单帧最大 64 字节

### 容错三件套
1. **大小写不敏感**：命令/子命令/参数名均可混合大小写
2. **空格容错**：命令与参数间允许多个空格/Tab
3. **缩写规则**：大写字母为必输部分，小写可省略
   - 例：`MINute` → `MIN` / `MINU` / `MINUT` / `MINUTE` 均合法

### 命令速查

| 命令 | 示例 |
|:---|:---|
| `*RST` | 复位 |
| `*SET:DATE YEAR 24 MONTH 06 DATE 04` | 设置日期 |
| `*SET:TIME HOUR 12 MIN 30 SEC 00` | 设置时间 |
| `*SET:ALARM HOUR 07 MIN 00 SEC 00` | 设置闹钟 |
| `*SET:ALARM OFF` | 关闭闹钟 |
| `*SET:DISP ON` / `OFF` | 显示开关 |
| `*SET:FORMAT LEFT` / `RIGHT` | 流水方向 |
| `*SET:MSG HELLO` | 设置显示消息 |
| `*SET:BEEP 500` | 蜂鸣 (ms) |
| `*SET:LED FF` | 设置 LED |
| `*SET:KEY FUNC` | 模拟按键 |
| `*SET:MODE DAY` / `NIGHT` | 昼夜模式 |
| `*GET:DATE` / `*GET:TIME` / `*GET:ALARM` | 查询 |
| `*PING` | 心跳探测 → `*PONG <uptime_s>` |

### 事件报文 (S800→PC)

| 报文 | 触发条件 |
|:---|:---|
| `*EVT:KEY <NAME>` | 物理按键按下 |
| `*EVT:ALARM` / `*EVT:ALARM OFF` | 闹钟开始/停止 |
| `*EVT:EDIT <TYPE> <VALUE>` | 本地编辑保存 |
| `*EVT:DISP <8char> <dpHex>` | 显示变化 + **1Hz 心跳** |
| `*EVT:LED <hex2>` | LED 变化 + **1Hz 心跳** |
| `*EVT:MODE <STATE>` | 模式切换 |

## 技术栈

| 端 | 技术 |
|:---|:---|
| 板端 | TM4C129 + TivaWare DriverLib + C89 |
| PC 端 | Python 3.11 + PyQt5 + pyserial + python-dotenv + ntplib + requests |

## 开发说明

- 板端固件遵循 C89 规范：所有局部变量在函数开头声明
- I2C 地址使用 exp2.c 中的已验证值（TCA6424=0x22, PCA9557=0x18）
- 固件不使用动态内存分配（无 malloc/free）
- PC 端使用信号槽机制保证线程安全
- 串口通信在独立 QThread 中进行，不阻塞 UI

## 扩展阅读

- **[PROJECT.md](PROJECT.md)** — 最详细的中文项目文档：每个文件的用途、功能使用指南、完整协议参考、E1/E2 扩展功能、故障排查
