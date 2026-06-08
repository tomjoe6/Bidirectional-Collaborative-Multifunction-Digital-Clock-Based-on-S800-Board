# S800 综合修复计划 — 执行记录与待解决问题

## 已修复

| # | 问题 | 文件 |
|:---|:---|:---|
| 1 | UI 标签 "分""秒" → ":" | control_panel.py |
| 2 | 参数组合下拉 5 种 | control_panel.py |
| 3 | LED 标签中文 + 字号 10px | led_indicator.py |
| 4 | 编辑孪生同步 (_on_edit_event) | main_window.py |
| 5 | FORMAT 状态栏跟踪 | twin_state.py |
| 6 | 闹钟状态栏 ON/OFF | twin_state.py |
| 7 | 错误弹窗（解析/S800/串口） | main_window.py |
| 8 | DP 半径 3px + 位置调整 | seven_seg.py |
| 9 | LED 标签宽度 28px 对齐 | led_indicator.py |
| 10 | 孪生段码补齐 A-Z 26 个字母 | seven_seg.py |
| 11 | RST 复位增强（时间+显示+方向） | protocol.c |
| 12 | FORMAT RIGHT 显示逆序 DP 正确 | display.c |
| 13 | 循环包裹仅 >8 字符生效 | display.c |
| 14 | DISP OFF 时 EVT:DISP 发空格 | main.c |
| 15 | 闹钟 8.5s 自动停止 | buzzer.c |
| 16 | SET LED 2s 用户优先锁 | protocol.c + main.c |
| 17 | GET DISP 不误触发闹钟 ON | twin_state.py |
| 18 | g_msg_timeout 冗余删除 | hw_config.h + main.c |
| 19 | Buzzer_WriteOutput 死代码删除 | buzzer.c + buzzer.h |
| 20 | 协议冒号解析顺序修复 | protocol.c |
| 21 | I2C 读改用 BURST repeated start | main.c |
| 22 | 按键映射纠正（全 K 在 TCA6424） | hw_config.h + keys.c |
| 23 | 开机动画版本号 + 时长优化 | main.c + display.c |
| 24 | 蜂鸣器从 PCA9557 改 PF3 PWM | buzzer.c |
| 25 | FORMAT RIGHT 响应逆序（SendOKData） | protocol.c |
| 26 | NTP ntplib 0.4.0 Unix 时间戳修正 | ntp_client.py |

## 待解决

| # | 问题 | 优先级 | 备注 |
|:---|:---|:---|:---|
| A | 闹钟自动停止 | 高 | 代码正确，需实测验证 |
| B | 蜂鸣器音量 | 中 | 2kHz Timer0 驱动，音量由硬件决定 |
| C | SW5 编辑时闪烁 | 中 | boot_guard 已保护，需实测 |
| D | 天气 API | 低 | Key 已激活但返回 401，需等 OpenWeatherMap 同步 |
| E | 流水 PC 刷新率 | 低 | 受 UART 带宽限制，已优化到 20ms/次 |
| F | 串口回环干扰 | 低 | 疑似 TX→RX 回环，需硬件排查 |
