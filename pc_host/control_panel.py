"""Control panel widget for sending commands to the S800 board.

Provides grouped controls for SET, GET, and demo commands.
All buttons emit formatted command strings via the send_command signal.
"""

from __future__ import annotations

import re

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import (
    QComboBox,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)


class ControlPanel(QWidget):
    """Control panel for sending protocol commands to the S800 board.

    Organized into three groups: 控制面板 (SET commands), 演示 (demo),
    and 查询 (GET commands).
    """

    send_command = pyqtSignal(str)
    ntp_sync_requested = pyqtSignal()        # E1: NTP time sync button clicked
    weather_fetch_requested = pyqtSignal()   # E2: weather fetch button clicked

    def __init__(self, parent: QWidget = None) -> None:
        """Initialize the control panel.

        Args:
            parent: Optional parent widget.
        """
        super().__init__(parent)
        self._setup_ui()

    def _setup_ui(self) -> None:
        """Build the complete control panel layout."""
        main_layout = QVBoxLayout(self)
        main_layout.setSpacing(8)

        # --- Group 1: 控制面板 (SET commands) ---
        self.grp_set = QGroupBox("控制面板")
        set_layout = QVBoxLayout(self.grp_set)
        set_layout.setSpacing(4)

        # Date row
        date_layout = QHBoxLayout()
        date_layout.addWidget(QLabel("日期预设:"))
        self.cmb_date_preset = QComboBox()
        self.cmb_date_preset.addItem("-- 选择 --", "")
        self.cmb_date_preset.addItem("闰年2月29→3.1 (2024-02-29)", "24 02 29")
        self.cmb_date_preset.addItem("平年2月28→3.1 (2025-02-28)", "25 02 28")
        self.cmb_date_preset.addItem("大月31→下月1 (2024-01-31)", "24 01 31")
        self.cmb_date_preset.addItem("小月30→下月1 (2024-04-30)", "24 04 30")
        self.cmb_date_preset.addItem("跨年进位 (2024-12-31)", "24 12 31")
        self.cmb_date_preset.currentIndexChanged.connect(self._on_date_preset)
        date_layout.addWidget(self.cmb_date_preset)

        date_layout.addWidget(QLabel("年:"))
        self.spin_year = QSpinBox()
        self.spin_year.setRange(0, 99)
        self.spin_year.setValue(24)
        self.spin_year.setPrefix("20")
        date_layout.addWidget(self.spin_year)

        date_layout.addWidget(QLabel("月:"))
        self.spin_month = QSpinBox()
        self.spin_month.setRange(1, 12)
        self.spin_month.setValue(6)
        date_layout.addWidget(self.spin_month)

        date_layout.addWidget(QLabel("日:"))
        self.spin_day = QSpinBox()
        self.spin_day.setRange(1, 31)
        self.spin_day.setValue(4)
        date_layout.addWidget(self.spin_day)

        self.btn_set_date = QPushButton("设置日期")
        self.btn_set_date.clicked.connect(self._on_set_date)
        date_layout.addWidget(self.btn_set_date)
        set_layout.addLayout(date_layout)

        # Time row
        time_layout = QHBoxLayout()
        time_layout.addWidget(QLabel("时间:"))
        self.spin_hour = QSpinBox()
        self.spin_hour.setRange(0, 23)
        self.spin_hour.setValue(12)
        time_layout.addWidget(self.spin_hour)

        time_layout.addWidget(QLabel(":"))
        self.spin_min = QSpinBox()
        self.spin_min.setRange(0, 59)
        self.spin_min.setValue(0)
        time_layout.addWidget(self.spin_min)

        time_layout.addWidget(QLabel(":"))
        self.spin_sec = QSpinBox()
        self.spin_sec.setRange(0, 59)
        self.spin_sec.setValue(0)
        time_layout.addWidget(self.spin_sec)

        self.btn_set_time = QPushButton("设置时间")
        self.btn_set_time.clicked.connect(self._on_set_time)
        time_layout.addWidget(self.btn_set_time)
        set_layout.addLayout(time_layout)

        # Parameter combo: mixed params demo
        combo_layout = QHBoxLayout()
        combo_layout.addWidget(QLabel("参数组合:"))
        self.cmb_param_combo = QComboBox()
        self.cmb_param_combo.addItem("完整 (HOUR MIN SEC)", "full")
        self.cmb_param_combo.addItem("仅时 (HOUR)", "hour")
        self.cmb_param_combo.addItem("仅分 (MIN)", "min")
        self.cmb_param_combo.addItem("仅秒 (SEC)", "sec")
        self.cmb_param_combo.addItem("时+分 (HOUR MIN)", "hourmin")
        combo_layout.addWidget(self.cmb_param_combo)
        combo_layout.addStretch()
        set_layout.addLayout(combo_layout)

        # Alarm row
        alarm_layout = QHBoxLayout()
        alarm_layout.addWidget(QLabel("闹钟:"))
        self.spin_alm_hour = QSpinBox()
        self.spin_alm_hour.setRange(0, 23)
        self.spin_alm_hour.setValue(7)
        alarm_layout.addWidget(self.spin_alm_hour)

        alarm_layout.addWidget(QLabel(":"))
        self.spin_alm_min = QSpinBox()
        self.spin_alm_min.setRange(0, 59)
        self.spin_alm_min.setValue(0)
        alarm_layout.addWidget(self.spin_alm_min)

        alarm_layout.addWidget(QLabel(":"))
        self.spin_alm_sec = QSpinBox()
        self.spin_alm_sec.setRange(0, 59)
        self.spin_alm_sec.setValue(0)
        alarm_layout.addWidget(self.spin_alm_sec)

        self.btn_set_alarm = QPushButton("设置闹钟")
        self.btn_set_alarm.clicked.connect(self._on_set_alarm)
        alarm_layout.addWidget(self.btn_set_alarm)

        self.btn_alarm_off = QPushButton("关闭闹钟")
        self.btn_alarm_off.clicked.connect(self._on_alarm_off)
        alarm_layout.addWidget(self.btn_alarm_off)
        set_layout.addLayout(alarm_layout)

        # Display row
        disp_layout = QHBoxLayout()
        disp_layout.addWidget(QLabel("显示:"))
        self.btn_disp_on = QPushButton("开启")
        self.btn_disp_on.clicked.connect(self._on_disp_on)
        disp_layout.addWidget(self.btn_disp_on)

        self.btn_disp_off = QPushButton("关闭")
        self.btn_disp_off.clicked.connect(self._on_disp_off)
        disp_layout.addWidget(self.btn_disp_off)
        disp_layout.addStretch()
        set_layout.addLayout(disp_layout)

        # Format row
        fmt_layout = QHBoxLayout()
        fmt_layout.addWidget(QLabel("流水方向:"))
        self.btn_format_left = QPushButton("从左到右")
        self.btn_format_left.clicked.connect(self._on_format_left)
        fmt_layout.addWidget(self.btn_format_left)

        self.btn_format_right = QPushButton("从右到左")
        self.btn_format_right.clicked.connect(self._on_format_right)
        fmt_layout.addWidget(self.btn_format_right)
        fmt_layout.addStretch()
        set_layout.addLayout(fmt_layout)

        # Message row
        msg_layout = QHBoxLayout()
        msg_layout.addWidget(QLabel("消息:"))
        self.txt_msg = QLineEdit()
        self.txt_msg.setMaxLength(32)
        self.txt_msg.setPlaceholderText("最多32个字符")
        msg_layout.addWidget(self.txt_msg)

        self.btn_send_msg = QPushButton("发送消息")
        self.btn_send_msg.clicked.connect(self._on_send_msg)
        msg_layout.addWidget(self.btn_send_msg)
        set_layout.addLayout(msg_layout)

        # Beep row
        beep_layout = QHBoxLayout()
        beep_layout.addWidget(QLabel("蜂鸣:"))
        self.spin_beep_ms = QSpinBox()
        self.spin_beep_ms.setRange(10, 5000)
        self.spin_beep_ms.setValue(500)
        self.spin_beep_ms.setSuffix(" ms")
        beep_layout.addWidget(self.spin_beep_ms)

        self.btn_beep = QPushButton("蜂鸣")
        self.btn_beep.clicked.connect(self._on_beep)
        beep_layout.addWidget(self.btn_beep)
        beep_layout.addStretch()
        set_layout.addLayout(beep_layout)

        # LED hex row
        led_layout = QHBoxLayout()
        led_layout.addWidget(QLabel("LED值:"))
        self.txt_led_hex = QLineEdit()
        self.txt_led_hex.setMaxLength(2)
        self.txt_led_hex.setPlaceholderText("00-FF")
        self.txt_led_hex.setFixedWidth(50)
        led_layout.addWidget(self.txt_led_hex)

        self.btn_set_led = QPushButton("设置 LED")
        self.btn_set_led.clicked.connect(self._on_set_led)
        led_layout.addWidget(self.btn_set_led)
        led_layout.addStretch()
        set_layout.addLayout(led_layout)

        # Mode row
        mode_layout = QHBoxLayout()
        mode_layout.addWidget(QLabel("模式:"))
        self.btn_mode_day = QPushButton("白天模式")
        self.btn_mode_day.clicked.connect(self._on_mode_day)
        mode_layout.addWidget(self.btn_mode_day)

        self.btn_mode_night = QPushButton("夜间模式")
        self.btn_mode_night.clicked.connect(self._on_mode_night)
        mode_layout.addWidget(self.btn_mode_night)
        mode_layout.addStretch()
        set_layout.addLayout(mode_layout)

        # --- E1/E2 extended features row ---
        ext_layout = QHBoxLayout()
        self.btn_ntp_sync = QPushButton("⏱ NTP 对时")
        self.btn_ntp_sync.setFixedWidth(120)
        self.btn_ntp_sync.setToolTip("从互联网NTP服务器获取精确时间并下发至S800板")
        self.btn_ntp_sync.clicked.connect(self._on_ntp_sync)
        ext_layout.addWidget(self.btn_ntp_sync)

        self.btn_weather = QPushButton("🌤 获取天气")
        self.btn_weather.setFixedWidth(120)
        self.btn_weather.setToolTip("从天气API获取实时天气数据")
        self.btn_weather.clicked.connect(self._on_weather_fetch)
        ext_layout.addWidget(self.btn_weather)

        self.lbl_weather_age = QLabel("")
        self.lbl_weather_age.setStyleSheet("color: #888888; font-size: 11px;")
        ext_layout.addWidget(self.lbl_weather_age)
        ext_layout.addStretch()
        set_layout.addLayout(ext_layout)

        # General commands row
        gen_layout = QHBoxLayout()
        self.btn_rst = QPushButton("复位")
        self.btn_rst.setStyleSheet("QPushButton { background-color: #8B0000; color: white; }")
        self.btn_rst.clicked.connect(self._on_rst)
        gen_layout.addWidget(self.btn_rst)

        self.btn_ping = QPushButton("PING")
        self.btn_ping.clicked.connect(self._on_ping)
        gen_layout.addWidget(self.btn_ping)
        gen_layout.addStretch()
        set_layout.addLayout(gen_layout)

        # Manual command row
        manual_layout = QHBoxLayout()
        manual_layout.addWidget(QLabel("手动命令:"))
        self.txt_manual_cmd = QLineEdit()
        self.txt_manual_cmd.setPlaceholderText("*PING / *GET:TIME / *SET:MSG Hello")
        self.txt_manual_cmd.returnPressed.connect(self._on_manual_command)
        manual_layout.addWidget(self.txt_manual_cmd)

        self.btn_manual_send = QPushButton("发送")
        self.btn_manual_send.clicked.connect(self._on_manual_command)
        manual_layout.addWidget(self.btn_manual_send)
        set_layout.addLayout(manual_layout)

        main_layout.addWidget(self.grp_set)

        # --- Group 2: 演示 (Demo) ---
        self.grp_demo = QGroupBox("演示")
        demo_layout = QVBoxLayout(self.grp_demo)
        demo_layout.setSpacing(2)

        # Small annotation explaining what these demo buttons test
        lbl_demo_hint = QLabel("验证协议容错三件套：缩写规则 / 大小写不敏感")
        lbl_demo_hint.setStyleSheet(
            "color: #888888; font-size: 10px; padding: 0px;"
        )
        demo_layout.addWidget(lbl_demo_hint)

        btn_row = QHBoxLayout()
        self.btn_abbrev_demo = QPushButton("缩写命令演示")
        self.btn_abbrev_demo.setToolTip(
            "发送 *SET:TIME ... MIN 00 SEC 00\n"
            "MIN→MINute 缩写，只大写必输"
        )
        self.btn_abbrev_demo.clicked.connect(self._on_abbrev_demo)
        btn_row.addWidget(self.btn_abbrev_demo)

        self.btn_case_demo = QPushButton("大小写混合演示")
        self.btn_case_demo.setToolTip(
            "发送 *SeT:TiMe ... MiNuTe ... SeCoNd\n"
            "验证命令大小写不敏感"
        )
        self.btn_case_demo.clicked.connect(self._on_case_demo)
        btn_row.addWidget(self.btn_case_demo)
        btn_row.addStretch()
        demo_layout.addLayout(btn_row)

        demo_layout.addStretch()
        main_layout.addWidget(self.grp_demo)

        # --- Group 3: 查询 (GET commands) ---
        self.grp_get = QGroupBox("查询")
        get_layout = QHBoxLayout(self.grp_get)

        self.btn_get_date = QPushButton("获取日期")
        self.btn_get_date.clicked.connect(self._on_get_date)
        get_layout.addWidget(self.btn_get_date)

        self.btn_get_time = QPushButton("获取时间")
        self.btn_get_time.clicked.connect(self._on_get_time)
        get_layout.addWidget(self.btn_get_time)

        self.btn_get_alarm = QPushButton("获取闹钟")
        self.btn_get_alarm.clicked.connect(self._on_get_alarm)
        get_layout.addWidget(self.btn_get_alarm)

        self.btn_get_disp = QPushButton("获取显示")
        self.btn_get_disp.clicked.connect(self._on_get_disp)
        get_layout.addWidget(self.btn_get_disp)

        self.btn_get_format = QPushButton("获取格式")
        self.btn_get_format.clicked.connect(self._on_get_format)
        get_layout.addWidget(self.btn_get_format)

        get_layout.addStretch()
        main_layout.addWidget(self.grp_get)

        main_layout.addStretch()

    # --- Command formatting helpers ---

    def _fmt(self, cmd: str, sub: str = None, params: list = None) -> str:
        """Format a protocol command string."""
        from protocol import ProtocolParser
        return ProtocolParser.format_command(cmd, sub, params)

    def _emit_cmd(self, cmd_str: str) -> None:
        """Emit a command and append CRLF."""
        self.send_command.emit(cmd_str + "\r\n")

    # --- Button handlers ---

    def _on_date_preset(self) -> None:
        """Apply date preset to spin boxes."""
        val = self.cmb_date_preset.currentData()
        if val:
            parts = val.split()
            if len(parts) == 3:
                self.spin_year.setValue(int(parts[0]))
                self.spin_month.setValue(int(parts[1]))
                self.spin_day.setValue(int(parts[2]))

    def _on_set_date(self) -> None:
        y = f"{self.spin_year.value():02d}"
        m = f"{self.spin_month.value():02d}"
        d = f"{self.spin_day.value():02d}"
        self._emit_cmd(self._fmt("SET", "DATE", ["YEAR", y, "MONTH", m, "DATE", d]))

    def _on_set_time(self) -> None:
        h = f"{self.spin_hour.value():02d}"
        m = f"{self.spin_min.value():02d}"
        s = f"{self.spin_sec.value():02d}"
        mode = self.cmb_param_combo.currentData()
        if mode == "hour":
            params = ["HOUR", h]
        elif mode == "min":
            params = ["MIN", m]
        elif mode == "sec":
            params = ["SEC", s]
        elif mode == "hourmin":
            params = ["HOUR", h, "MIN", m]
        else:  # "full"
            params = ["HOUR", h, "MIN", m, "SEC", s]
        self._emit_cmd(self._fmt("SET", "TIME", params))

    def _on_set_alarm(self) -> None:
        h = f"{self.spin_alm_hour.value():02d}"
        m = f"{self.spin_alm_min.value():02d}"
        s = f"{self.spin_alm_sec.value():02d}"
        self._emit_cmd(self._fmt("SET", "ALARM", ["HOUR", h, "MIN", m, "SEC", s]))

    def _on_alarm_off(self) -> None:
        self._emit_cmd(self._fmt("SET", "ALARM", ["OFF"]))

    def _on_disp_on(self) -> None:
        self._emit_cmd(self._fmt("SET", "DISP", ["ON"]))

    def _on_disp_off(self) -> None:
        self._emit_cmd(self._fmt("SET", "DISP", ["OFF"]))

    def _on_format_left(self) -> None:
        self._emit_cmd(self._fmt("SET", "FORMAT", ["LEFT"]))

    def _on_format_right(self) -> None:
        self._emit_cmd(self._fmt("SET", "FORMAT", ["RIGHT"]))

    def _on_send_msg(self) -> None:
        msg = self.txt_msg.text().strip()
        if msg:
            self._emit_cmd(self._fmt("SET", "MSG", [msg]))

    def _on_beep(self) -> None:
        ms = str(self.spin_beep_ms.value())
        self._emit_cmd(self._fmt("SET", "BEEP", [ms]))

    def _on_set_led(self) -> None:
        val = self.txt_led_hex.text().strip().upper()
        if val and re.match(r"^[0-9A-F]{1,2}$", val):
            self._emit_cmd(self._fmt("SET", "LED", [val]))

    def _on_mode_day(self) -> None:
        self._emit_cmd(self._fmt("SET", "MODE", ["DAY"]))

    def _on_mode_night(self) -> None:
        self._emit_cmd(self._fmt("SET", "MODE", ["NIGHT"]))

    def _on_rst(self) -> None:
        self._emit_cmd(self._fmt("RST"))

    def _on_ping(self) -> None:
        self._emit_cmd(self._fmt("PING"))

    def _on_manual_command(self) -> None:
        cmd = self.txt_manual_cmd.text().strip()
        if not cmd:
            return
        self.send_command.emit(cmd + "\r\n")

    def _on_abbrev_demo(self) -> None:
        """Send abbreviated commands demonstrating MIN->MINute, SEC->SECond tolerance."""
        h = f"{self.spin_hour.value():02d}"
        m = f"{self.spin_min.value():02d}"
        s = f"{self.spin_sec.value():02d}"
        # Use abbreviated forms: MIN for MINute, SEC for SECond
        self.send_command.emit(f"*SET:TIME HOUR {h} MIN {m} SEC {s}\r\n")

    def _on_case_demo(self) -> None:
        """Send mixed-case command demonstrating case insensitivity."""
        h = f"{self.spin_hour.value():02d}"
        m = f"{self.spin_min.value():02d}"
        s = f"{self.spin_sec.value():02d}"
        self.send_command.emit(f"*SeT:TiMe HoUr {h} MiNuTe {m} SeCoNd {s}\r\n")

    # --- E1/E2 extension handlers ---

    def _on_ntp_sync(self) -> None:
        """Emit NTP sync request signal (handled by MainWindow)."""
        self.ntp_sync_requested.emit()

    def _on_weather_fetch(self) -> None:
        """Emit weather fetch request signal (handled by MainWindow)."""
        self.weather_fetch_requested.emit()

    def set_ntp_enabled(self, enabled: bool) -> None:
        """Enable or disable the NTP sync button.

        Args:
            enabled: True to enable, False to disable (gray out).
        """
        self.btn_ntp_sync.setEnabled(enabled)

    def set_weather_enabled(self, enabled: bool) -> None:
        """Enable or disable the weather fetch button.

        Args:
            enabled: True to enable, False to disable (gray out).
        """
        self.btn_weather.setEnabled(enabled)

    def set_weather_age(self, age_text: str) -> None:
        """Update the weather cache age label.

        Args:
            age_text: Human-readable age string (e.g. '12分钟前').
        """
        self.lbl_weather_age.setText(age_text)

    # --- GET handlers ---

    def _on_get_date(self) -> None:
        self._emit_cmd(self._fmt("GET", "DATE"))

    def _on_get_time(self) -> None:
        self._emit_cmd(self._fmt("GET", "TIME"))

    def _on_get_alarm(self) -> None:
        self._emit_cmd(self._fmt("GET", "ALARM"))

    def _on_get_disp(self) -> None:
        self._emit_cmd(self._fmt("GET", "DISP"))

    def _on_get_format(self) -> None:
        self._emit_cmd(self._fmt("GET", "FORMAT"))
