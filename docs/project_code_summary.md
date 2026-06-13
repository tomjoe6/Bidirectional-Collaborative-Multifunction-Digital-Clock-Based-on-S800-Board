# S800 智能联网时钟项目关键代码与功能总结

本文档总结当前项目的核心代码片段、实现亮点、难点解决方案，以及选做功能“节日祝福”和“语音控制”的设计、实现与演示说明。

## 1. 项目整体结构

本项目分为 **S800 板端固件** 和 **PC 上位机** 两部分，通过 USB 虚拟串口进行双向协作。

| 部分 | 主要文件 | 职责 |
| --- | --- | --- |
| 板端固件 | `firmware/main.c`、`firmware/*.h` | 时钟走时、闹钟、数码管显示、按键扫描、蜂鸣器、LED、串口协议与事件上报 |
| PC 上位机 | `pc_host/main.py`、`pc_host/ui_main_window.py` | PyQt5 图形界面、串口连接、控制面板、数字孪生、日志、扩展功能 |
| 通信协议 | `firmware/main.c`、`pc_host/protocol.py` | ASCII 命令/响应协议，支持 `*SET`、`*GET`、`*PING`、`*EVT` |
| 选做功能 | `pc_host/holiday.py`、`pc_host/voice_thread.py` | 节日祝福自动下发、离线语音识别控制 |

核心通信链路：

```text
用户操作 / PC扩展功能
        ↓
PC上位机 PyQt5 界面
        ↓  USB虚拟串口 ASCII协议
S800板端 UART0协议解析
        ↓
时钟 / 闹钟 / 显示 / 按键 / 蜂鸣 / LED
        ↓
*EVT事件上报，PC数字孪生同步显示
```

## 2. 关键代码片段

### 2.1 板端 1ms 调度框架

板端没有操作系统，因此用 `SysTick_Handler()` 产生 1ms 时基，再在主循环中按 `1ms / 10ms / 100ms / 1000ms` 分层执行任务。

```c
void SysTick_Handler(void)
{
    g_flag_1ms = 1;

    if (g_cnt_10ms > 0) {
        g_cnt_10ms--;
    } else {
        g_cnt_10ms = (uint16_t)(SYSTICK_FREQUENCY / 100 - 1);
        g_flag_10ms = 1;
        ...
    }
}
```

主循环根据标志位调度不同频率任务：

```c
if (g_flag_1ms) {
    g_flag_1ms = 0;
    Display_Scan(scan_idx);
}

if (g_flag_10ms) {
    g_flag_10ms = 0;
    Keys_Scan10ms();
    Buzzer_RhythmHandler();
    LED_UpdateFlashTimeout();
    Events_SendNext();
}

if (g_flag_1000ms) {
    g_flag_1000ms = 0;
    Clock_Tick();
    Alarm_Check(&clock_now);
    Events_1HzHandler(&clock_now, led_for_evt, disp_str, g_dp_mask);
}
```

作用：

- `1ms`：数码管动态扫描，保证显示稳定。
- `10ms`：按键消抖、蜂鸣器节奏、LED 闪烁、事件队列发送。
- `1000ms`：时钟走时、闹钟检查、心跳事件上报。

### 2.2 UART 中断收包 + 主循环解析

串口接收在中断里只做“收字符入环形缓冲区”，真正解析放到主循环，避免 ISR 中执行复杂逻辑。

```c
void UART0_Handler(void)
{
    ...
    while (UARTCharsAvail(UART0_BASE)) {
        recv_char = UARTCharGetNonBlocking(UART0_BASE);
        if (recv_char >= 0) {
            Protocol_CharReceived((uint8_t)recv_char);
        }
    }
}
```

```c
void Protocol_CharReceived(uint8_t c)
{
    uint16_t next_head = g_ring_head + 1;
    if (next_head >= PROTO_RING_SIZE) {
        next_head = 0;
    }

    if (next_head == g_ring_tail) {
        g_ring_overflow = 1;
        return;
    }

    g_ring_buf[g_ring_head] = c;
    g_ring_head = next_head;
    LED_RXFlash();
}
```

难点处理：

- 环形缓冲防止串口突发数据丢失。
- 溢出时置位 `g_ring_overflow`，主循环统一回复 `ERROR` 并复位缓冲。
- ISR 只做轻量工作，降低中断阻塞风险。

### 2.3 ASCII 协议解析与命令分发

板端协议统一要求命令以 `*` 开头，例如：

```text
*SET:TIME HOUR 12 MIN 30 SEC 00
*SET:ALARM HOUR 07 MIN 00 SEC 00
*SET:MSG HappyNew
*GET:TIME
*PING
```

解析入口：

```c
static void Protocol_ParseLine(char *line)
{
    if (*p != '*') {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }
    p++;

    cmd_str = NextToken(&p);

    if (MatchAbbrev(cmd_str, "RST")) {
        Cmd_RST();
        return;
    }

    if (MatchAbbrev(cmd_str, "PING")) {
        Cmd_PING();
        return;
    }

    ...
}
```

亮点是支持缩写匹配：

```c
static uint8_t MatchAbbrev(const char *input, const char *pattern)
{
    /* uppercase=required, lowercase=optional
       e.g. MINute 可匹配 MIN / MINU / MINUTE */
}
```

这样 `MINute`、`DISPlay` 这类参数可以容错匹配，增强了手动调试和上位机命令生成的兼容性。

### 2.4 消息显示命令 `*SET:MSG`

天气、节日祝福等扩展功能最终都复用板端消息显示能力。

```c
static void Cmd_SET_MSG(char *params)
{
    char *text = SkipSpaces(params);
    if (text == NULL || *text == '\0') {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    if (strlen(text) > 32) {
        Protocol_SendResponse("ERROR\r\n");
        return;
    }

    Display_SetBuffer(text);
    if (strlen(text) <= DISP_DIGITS) {
        g_msg_timeout = 300; /* 3s, in 10ms ticks */
    }
    Protocol_SendResponse("OK\r\n");
}
```

设计意义：

- 上位机只需要下发一条 `*SET:MSG`，板端负责显示。
- 短文本自动 3 秒后恢复时钟显示。
- 长文本进入流水显示模式。

### 2.5 PC 端串口线程与协议解析

PC 端用 `SerialWorker(QThread)` 独立处理串口收发，避免 GUI 卡顿。

```python
class SerialWorker(QThread):
    connected = pyqtSignal(bool, str)
    received = pyqtSignal(bytes)
    error = pyqtSignal(str)

    def run(self) -> None:
        self._serial = serial.Serial(...)
        while self._running and self._serial and self._serial.is_open:
            self._flush_send_buffer()
            if self._serial.in_waiting > 0:
                data = self._serial.read(self._serial.in_waiting)
                self.received.emit(data)
            else:
                self.msleep(10)
```

收到原始串口数据后，主窗口交给 `ProtocolParser` 解析，并同步到数字孪生：

```python
frames = self._parser.parse_incoming(data)
for frame in frames:
    if self._parser.is_heartbeat_frame(frame):
        self._heartbeat.feed()

    if frame.get("type") == "pong":
        self._heartbeat.record_pong_received()

    self._twin_state.process_frame(frame)
```

### 2.6 数字孪生状态同步

板端每秒主动上报显示和 LED 心跳：

```c
void Events_1HzHandler(ClockTime *now, uint8_t led_state,
                       const char *disp_str, uint8_t dp)
{
    Events_ReportDisp(disp_str, dp);
    Events_ReportLED(led_state);
}
```

PC 端将事件转换为界面更新：

```python
if ftype == "evt_disp":
    self._seg_text = frame.get("seg_text", self._seg_text)
    self._dp_hex = frame.get("dp_hex", self._dp_hex)
    self.seg_changed.emit(self._seg_text, self._dp_hex)

elif ftype == "evt_led":
    self._led_byte = frame.get("led_byte", self._led_byte)
    self.led_changed.emit(self._led_byte)
```

效果：

- 板端数码管内容变化后，PC 上位机的 7 段数码管同步变化。
- 板端 LED 状态变化后，PC 上位机 LED 条同步变化。
- 心跳事件同时用于判断连接是否还活着。

## 3. 项目亮点

### 3.1 板端独立运行 + PC 双向协作

S800 板端不依赖 PC 也能完成时间显示、闹钟、按键编辑、蜂鸣和 LED 状态管理。PC 接入后，可以远程控制板端，同时接收板端事件并生成 1:1 数字孪生显示。

### 3.2 统一 ASCII 协议，便于调试和扩展

命令全部是可读文本，例如：

```text
*SET:TIME HOUR 12 MIN 30 SEC 00
OK
*EVT:DISP 12_30_00 14
*EVT:LED 3F
```

优点：

- 串口助手可直接手动测试。
- PC 上位机容易生成命令。
- 后续扩展新命令成本低。

### 3.3 数字孪生不是简单 UI，而是事件驱动状态镜像

PC 界面不是单纯显示本地输入，而是根据板端 `*EVT` 事件更新，因此能够反映真实硬件状态。

### 3.4 扩展功能尽量复用底层协议

节日祝福、天气、语音闹钟等功能没有重新设计板端大模块，而是复用已有命令：

- 节日祝福：`*SET:MSG <text>`
- 天气显示：`*SET:MSG <weather>`
- 语音设闹钟：`*SET:ALARM HOUR xx MIN yy SEC 00`
- 语音查天气：触发天气获取并下发 `*SET:MSG`

这种设计降低了板端代码复杂度，也方便演示。

## 4. 难点与解决方案

### 4.1 数码管动态扫描与主循环任务冲突

难点：数码管需要高频扫描，串口解析、按键、闹钟等任务也要同时运行。

解决：

- 用 `SysTick` 产生稳定 1ms 时基。
- `Display_Scan()` 放在 1ms 任务中。
- 其他任务拆到 10ms、1000ms，避免阻塞显示。

### 4.2 UART 收包不能阻塞中断

难点：如果在 UART 中断里直接解析整条命令，容易导致中断时间过长。

解决：

- ISR 只负责把字符放入环形缓冲。
- 主循环中的 `Protocol_Process()` 组包、识别 `\r\n`、再分发命令。
- 环形缓冲溢出后统一返回 `ERROR`。

### 4.3 TCA6424 按键上电抖动/漂浮导致误触发

难点：上电后扩展按键可能出现短暂不稳定，导致显示模式或流水方向被误切换。

解决：

- 10ms 周期扫描并消抖。
- 开机动画结束后加入 `boot_guard`，约 1.5 秒内强制恢复 `TIME + LEFT`。
- 编辑状态优先，避免正常用户编辑被强行覆盖。

### 4.4 PC GUI 与串口收发并发

难点：串口读写和网络请求可能导致 GUI 假死。

解决：

- 串口读写放入 `SerialWorker(QThread)`。
- 使用 Qt 信号把数据发回主线程。
- 心跳监控用 `QTimer` 独立检测 `EVT:DISP` 和 `EVT:LED`。

### 4.5 扩展功能与 8 位数码管显示限制

难点：数码管只有 8 位，且字符表达能力有限。

解决：

- 节日祝福统一设计为 8 字符以内 ASCII，例如 `HappyNew`、`NationDy`。
- 天气显示压缩为短字符串，例如 `Sunny26C`、`Rain20C`。
- 超过 8 字符时走板端流水显示。

## 5. 选做功能一：节日祝福

### 5.1 设计思路

节日祝福放在 PC 端实现，而不是固化到板端。

原因：

- 节日表可在 PC 端快速修改，不需要重新烧录固件。
- PC 端可以读取系统日期，判断当天是否为节日。
- 板端只需要复用 `*SET:MSG` 显示文本。

限制条件：

- 数码管显示能力有限，祝福语尽量控制在 8 个字符以内。
- 使用 ASCII 文本，保证能被板端段码表显示。
- 连接成功后延迟下发，避免被板端开机动画覆盖。

### 5.2 实现代码

节日映射表在 `pc_host/holiday.py`：

```python
HOLIDAYS = {
    (1,  1):  "HappyNew",
    (2, 14):  "Love You",
    (5,  1):  "51Happy ",
    (6,  1):  "61Happy ",
    (9, 10):  "Teachers",
    (10, 1):  "NationDy",
    (10, 2):  "NationDy",
    (10, 3):  "NationDy",
    (12, 25): "MerryXma",
}

def get_greeting() -> str:
    today = date.today()
    return HOLIDAYS.get((today.month, today.day), "")
```

连接成功后，主窗口自动检查节日并延迟 8 秒下发：

```python
from holiday import get_greeting
greeting = get_greeting()
if greeting:
    def _send_greeting():
        cmd = f"*SET:MSG {greeting}\r\n"
        if self._serial_worker and self._serial_worker.is_connected():
            self._serial_worker.send(cmd.encode("ascii"))
            self.log_panel.addEntry("TX", f"节日祝福: {greeting}")
    QTimer.singleShot(8000, _send_greeting)
else:
    self.log_panel.addEntry("EVT", "节日检查: 今天不是特殊节日")
```

### 5.3 演示说明

演示步骤：

1. 连接 S800 板端串口。
2. 如果当天是节日，等待约 8 秒。
3. PC 日志显示 `节日祝福: <内容>`。
4. S800 数码管显示对应祝福语。

如果演示当天不是节日，可以临时在 `HOLIDAYS` 中加入当天日期，例如：

```python
(6, 13): "HappyDay"
```

演示话术：

> 节日祝福功能采用 PC 端日期判断，检测到节日后自动通过串口下发 `*SET:MSG`。板端不需要知道节日规则，只负责显示消息，因此后续新增节日只改 PC 端映射表即可。

## 6. 选做功能二：语音控制

### 6.1 设计思路

语音控制采用 PC 端离线识别，板端仍然复用原有串口命令。

设计链路：

```text
麦克风输入
  ↓
SpeechRecognition 采集音频
  ↓
Vosk 中文离线模型识别文本
  ↓
正则匹配语音意图
  ↓
Qt 信号发送到主窗口
  ↓
转换为 S800 串口命令
```

支持两类语音意图：

- 设置闹钟：例如“设七点三十分闹钟”“八点提醒我”。
- 查询天气：例如“今天天气怎么样”“气温多少”。

### 6.2 实现代码

语音线程在 `pc_host/voice_thread.py` 中实现：

```python
class VoiceWorker(QThread):
    recognized = pyqtSignal(str)
    alarm_cmd = pyqtSignal(int, int)
    weather_cmd = pyqtSignal()

    _TIME_RE = re.compile(
        r"([\d零一二三四五六七八九十两]+)\s*点"
        r"\s*([\d零一二三四五六七八九十两]*)\s*(?:分)?"
    )
    _ALARM_KW = re.compile(r"闹钟|提醒|叫我")
    _WEATHER_KW = re.compile(r"天气|气温")
```

运行时加载 Vosk 模型并监听麦克风：

```python
model_path = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "vosk-model-small-cn-0.22",
)
model = vosk.Model(model_path)
mic = sr.Microphone()

while self._running:
    with mic as source:
        audio = recognizer.listen(source, timeout=1, phrase_time_limit=5)
    rec = vosk.KaldiRecognizer(model, 16000)
    data = audio.get_raw_data(convert_rate=16000, convert_width=2)
    ...
```

识别到文本后分发意图：

```python
def _dispatch(self, text: str) -> None:
    if self._ALARM_KW.search(text):
        match = self._TIME_RE.search(text)
        if match:
            hour = self._parse_num(match.group(1))
            minute_text = (match.group(2) or "").strip()
            minute = self._parse_num(minute_text) if minute_text else 0
            if hour is not None and minute is not None:
                if 0 <= hour <= 23 and 0 <= minute <= 59:
                    self.alarm_cmd.emit(hour, minute)
                    return

    if self._WEATHER_KW.search(text):
        self.weather_cmd.emit()
```

主窗口接收信号后转换为串口命令：

```python
self._voice_worker.alarm_cmd.connect(self._on_voice_alarm)
self._voice_worker.weather_cmd.connect(self._on_weather_fetch)
```

```python
def _on_voice_alarm(self, hour: int, minute: int) -> None:
    cmd = f"*SET:ALARM HOUR {hour:02d} MIN {minute:02d} SEC 00\r\n"
    self.log_panel.addEntry("TX", f"语音设闹钟: {hour:02d}:{minute:02d}")
    if self._serial_worker and self._serial_worker.is_connected():
        self._serial_worker.send(cmd.encode("ascii"))
```

### 6.3 中文数字解析

语音识别结果可能是阿拉伯数字，也可能是中文数字，例如：

```text
7点30分
七点三十分
十点
二十三点五分
```

代码中用 `_parse_num()` 统一转换：

```python
if text in self._CN_NUM:
    return self._CN_NUM[text]

if "十" in text:
    left, _, right = text.partition("十")
    tens = self._CN_NUM.get(left, 1) if left else 1
    ones = self._CN_NUM.get(right, 0) if right else 0
    return tens * 10 + ones
```

### 6.4 演示说明

演示前准备：

1. 安装依赖：

```bash
pip install -r requirements.txt
```

2. 确认 Vosk 中文模型目录存在：

```text
pc_host/vosk-model-small-cn-0.22
```

3. 连接 S800 板端串口，打开 PC 上位机。

演示步骤：

1. 点击工具栏麦克风按钮。
2. 按钮变为红色，日志显示“语音: 正在监听...”。
3. 说：“设置七点三十分闹钟”。
4. PC 日志显示识别文本和 `语音设闹钟: 07:30`。
5. 板端收到：

```text
*SET:ALARM HOUR 07 MIN 30 SEC 00
```

6. 说：“天气怎么样”。
7. PC 触发天气获取，成功后通过 `*SET:MSG` 将天气短文本下发到数码管。

演示话术：

> 语音控制没有改变板端协议，而是在 PC 端完成离线识别和意图解析。识别到闹钟指令后转换为 `*SET:ALARM`，识别到天气指令后复用天气模块并通过 `*SET:MSG` 显示，因此语音功能和原有控制面板、串口协议完全兼容。

## 7. 可展示的完整演示流程

建议按以下顺序演示，逻辑最清楚：

1. **板端独立运行**
   - 上电后显示时钟。
   - 按键切换显示模式、编辑时间或闹钟。
   - 闹钟触发后蜂鸣器和 LED 状态变化。

2. **PC 上位机连接**
   - 选择串口并连接。
   - 数字孪生界面同步显示板端数码管和 LED。
   - 日志中持续看到 `*EVT:DISP`、`*EVT:LED`。

3. **协议控制**
   - 在控制面板设置时间、闹钟、LED、蜂鸣、消息。
   - 展示 PC 发出 `*SET:*` 命令，板端返回 `OK`。

4. **节日祝福**
   - 连接后自动检测日期。
   - 若命中节日，8 秒后自动下发 `*SET:MSG <祝福语>`。
   - 数码管显示祝福语。

5. **语音控制**
   - 点击麦克风按钮。
   - 说“设置七点三十分闹钟”。
   - 展示日志、板端闹钟状态变化。
   - 说“天气怎么样”，展示天气下发到数码管。

## 8. 总结

本项目的核心价值在于：

- 板端具备完整独立时钟能力。
- PC 上位机通过串口协议实现双向控制和数字孪生。
- 事件上报和心跳机制让 PC 显示真实反映板端状态。
- 扩展功能采用“PC 端智能 + 板端协议复用”的方式，减少固件复杂度。
- 节日祝福和语音控制都没有破坏原架构，而是自然接入 `*SET:MSG`、`*SET:ALARM`、天气模块和事件日志。
