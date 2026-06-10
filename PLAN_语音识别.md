# 语音识别指令 Plan

## 前置
- 板子无麦克风，全部工作由 PC 端完成
- PC 已有 PyQt5 串口通信基础设施，语音识别为独立线程，识别到指令后调现有方法

## 依赖
```
pip install speechrecognition pyaudio
```
- `speechrecognition`：封装 Google/离线语音识别
- `pyaudio`：PC 麦克风采集（Windows 需 pipwin 或手动装 wheel）

## 架构
```
PC 端：
  VoiceThread (QThread)
    ├── pyaudio 实时录音（循环采集片段）
    ├── speechrecognition 识别 → 文本
    ├── 关键词匹配 → 调 MainWindow 方法
    │     "对时"     → _on_ntp_sync()
    │     "设置时间X点Y分" → _on_send_command(构建的 *SET:TIME)
    │     "天气"     → _on_weather_fetch()
    │     "夜间模式" → 发送 *SET:MODE NIGHT
    │     "白天模式" → 发送 *SET:MODE DAY
    │     "显示日期" → 发送 *SET:DISP DATE
    │     "显示时间" → 发送 *SET:DISP TIME
    └── signal  → 日志显示 "语音: xxx"

板端：完全不改，命令仍走 *SET:*GET: 协议
```

## 文件清单
| 文件 | 作用 |
|:---|:---|
| pc_host/voice_thread.py | QThread：录音→识别→发信号 |
| pc_host/ui_main_window.py | 连接 VoiceThread 信号到现有方法 |

## 实现步骤
1. 创建 `voice_thread.py`：
   - `VoiceWorker(QThread)` 类
   - `run()` 循环：录音→识别→ `recognized.emit(text)`
   - 关键词映射 dict：中文→回调函数名
2. 在 `main_window.py`：
   - `__init__` 中启动 `VoiceWorker`
   - 连接 `recognized` 信号到 `_on_voice_command`
   - `_on_voice_command` 解析文本，分发到现有方法
3. 界面加麦克风图标按钮开关语音

## 工作量
约 80 行 Python，板端 0 改动。2 个新文件。

## 风险
- `pyaudio` 在 Windows 上安装可能需额外步骤（缺少 Visual C++ 运行时）
- 中文识别需联网（Google Speech API），或使用 `sphinx` 离线引擎（精度差）
