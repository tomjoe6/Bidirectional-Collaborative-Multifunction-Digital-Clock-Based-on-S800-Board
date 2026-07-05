# S800 双向协同多功能数字时钟

本目录是课程提交用工程目录，包含 TM4C129/S800 板端固件、PC 上位机和配置说明。系统通过 UART0 串口通信，实现时钟显示、日期/时间/闹钟设置、按键事件上报、数码管/LED 数字孪生、NTP 对时、天气显示和语音指令。

## 目录组成

```text
524031910727/
├── README.md                 # 本说明文档
├── docs/                     # 设计说明、视频等
├── mcu/                      # 板端 Keil 工程
│   ├── S.uvprojx             # Keil 工程文件
│   ├── S.uvoptx              # Keil 用户/调试配置
│   ├── src/main.c            # 板端主程序，已整合各功能模块
│   ├── Inc/                  # 头文件和芯片寄存器定义
│   ├── Driverlib/            # TI DriverLib
│   └── obj/                  # 预生成 hex/axf 等输出文件
└── pc_host/                  # PC 上位机
    ├── main.py               # PyQt 应用入口
    ├── run.py                # 上位机启动脚本
    ├── requirements.txt      # Python 依赖
    ├── .env.example          # NTP/天气等配置模板
    ├── vosk-model-small-cn-0.22/ # 离线中文语音识别模型
    └── *_helper.py, *_panel.py 等功能模块
```

## MCU 编译与烧录

### 环境要求

- Keil MDK-ARM 5
- TM4C1294NCPDT 相关 Device Pack
- S800/TM4C129 开发板
- Stellaris下载器

### 编译步骤

1. 打开 Keil。
2. 打开工程：

   ```text
   524031910727/mcu/S.uvprojx
   ```

3. 在 Keil 中选择目标 `Target 1`。
4. 点击 `Build` 。
5. 编译成功后，输出文件位于：

   ```text
   524031910727/mcu/obj/
   ```

### 烧录步骤

1. 连接开发板电源和下载器。
2. 在 Keil 中进入 `Options for Target`。
3. 检查 `Debug` 页的下载器选择是否与实际硬件一致。
   - 如果使用板载 ICDI，应选择 Stellaris/TI ICDI 相关驱动。
   - 如果误选 `ULINK2/ME`，但没有接 ULINK，会报 `No ULINK2/ME Device found`。
4. 进入 `Utilities` 页，确认使用同一个 Debug Driver 下载。
5. 点击 `Download` 烧录。

如果从其他目录复制工程后无法烧录，优先检查 `.uvoptx` 是否一起复制，以及 Keil 的 `Debug`/`Utilities` 下载器设置是否被重置。

## PC 上位机运行

### 环境要求

- Windows
- Python 3.11
- 可访问开发板 UART 串口
- 如需语音功能，需要麦克风和 PyAudio

### 安装依赖

在命令行进入上位机目录：

```powershell
cd 524031910727\pc_host
pip install -r requirements.txt
```

`pyaudio` 在 Windows 上安装失败时，可使用与你 Python 版本匹配的 wheel 包安装。

### 配置天气/NTP

如果不使用现成API，需要自行配置天气功能，将模板复制为 `.env`：

```powershell
copy .env.example .env
```

然后编辑 `.env`，至少配置：

```text
WEATHER_API_KEY=你的天气APIKey
WEATHER_LOCATION=Shanghai,CN
NTP_SERVER=ntp.aliyun.com
```

没有配置天气 API Key 时，上位机仍可运行，但天气获取功能不可用。

### 启动上位机

```powershell
python run.py
```

启动后在界面中选择串口并连接。默认串口通信参数为 `115200, 8N1`。

### 语音识别注意事项

>语音识别使用 `pc_host/vosk-model-small-cn-0.22` 离线模型。Vosk 在 Windows 下对中文路径兼容性较差，建议提交目录及其父目录使用纯英文路径，例如：

```text
X:\coding\S800Project\524031910727
```

如果点击语音按钮后日志出现 `Vosk模型加载失败: Failed to create a model`，优先检查路径是否包含中文、模型目录是否完整。

## 功能简介

板端固件主要功能：

- 1 ms SysTick 时基
- 8 位数码管动态扫描
- HH.MM.SS、YY.MM.DD、YYYY.MMDD 显示
- 长消息流水显示，支持方向和速度切换
- 10 个按键扫描、消抖、长按识别
- 日期、时间、闹钟编辑状态机
- 蜂鸣器闹钟提示
- LED 状态指示
- UART0 命令解析和事件上报

PC 上位机主要功能：

- 串口连接与命令发送
- 数码管和 LED 数字孪生显示
- 日期、时间、闹钟、显示内容、方向、蜂鸣、LED 控制
- 按键模拟
- NTP 网络对时
- 天气获取并下发到数码管
- 离线语音指令识别
- 通信日志和数据看板

## 常见问题

### 上位机能打开但天气不可用

检查 `.env` 中是否配置了 `WEATHER_API_KEY`，并确认网络可访问对应天气 API。

### 语音按钮启动后模型加载失败

确认 `vosk-model-small-cn-0.22` 目录存在且完整，并尽量将工程放到纯英文路径下运行。
