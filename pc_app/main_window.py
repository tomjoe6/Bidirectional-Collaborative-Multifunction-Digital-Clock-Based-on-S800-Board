"""Main application window for the S800 digital twin system.

Wires together serial communication, protocol parsing, state management,
heartbeat monitoring, and all GUI widgets.
"""

from __future__ import annotations

from typing import List, Optional

from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QFont
from PyQt5.QtWidgets import (
    QApplication,
    QComboBox,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from .config import (
    APP_NAME,
    APP_VERSION,
    DEFAULT_BAUD,
    DEFAULT_TIMEOUT,
    COLOR_ERR,
    COLOR_OK,
    NTP_LED_D6_MASK,
    NTP_LED_DURATION_MS,
    NTP_STATUS_DISPLAY_MS,
)
from .heartbeat import HeartbeatMonitor
from .ntp_client import NTPClient
from .protocol import ProtocolParser
from .serial_worker import SerialWorker
from .twin_state import TwinStateManager
from .weather_client import WeatherClient
from .widgets import ControlPanel, LEDBarWidget, LogPanel, SevenSegWidget


class MainWindow(QMainWindow):
    """Main window of the S800 Smart Clock Digital Twin PC application.

    Layout:
        - Top toolbar: COM port selection, connect/disconnect, status
        - Left panel: Digital twin display (7-seg, LEDs, key buttons)
        - Right panel: Control panel for sending commands
        - Bottom panel: Communication log
    """

    def __init__(self) -> None:
        """Initialize the main window and all subsystems."""
        super().__init__()
        self.setWindowTitle(f"{APP_NAME} v{APP_VERSION}")
        self.setMinimumSize(1100, 750)

        # Core subsystems
        self._parser = ProtocolParser()
        self._twin_state = TwinStateManager(self)
        self._heartbeat = HeartbeatMonitor(self)
        self._serial_worker: Optional[SerialWorker] = None

        # E1: NTP time sync client
        self._ntp_client = NTPClient()
        self._ntp_pending: bool = False
        self._ntp_ok_count: int = 0
        self._ntp_check_timer: Optional[QTimer] = None

        # E2: Weather client
        self._weather_client = WeatherClient()

        # Timer for auto-refreshing COM ports
        self._port_timer = QTimer(self)
        self._port_timer.setInterval(2000)

        self._setup_ui()
        self._connect_signals()
        self._refresh_ports()

    # ------------------------------------------------------------------
    # UI Setup
    # ------------------------------------------------------------------

    def _setup_ui(self) -> None:
        """Build the complete window layout."""
        central = QWidget()
        self.setCentralWidget(central)
        root_layout = QVBoxLayout(central)
        root_layout.setContentsMargins(6, 6, 6, 6)
        root_layout.setSpacing(4)

        # --- Top toolbar ---
        root_layout.addLayout(self._create_toolbar())

        # --- Main content: splitter with left (twin) + right (control) ---
        splitter = QSplitter(Qt.Horizontal)

        splitter.addWidget(self._create_twin_panel())
        splitter.addWidget(self._create_right_panel())

        # Give the twin panel 40% and control 60% roughly
        splitter.setStretchFactor(0, 2)
        splitter.setStretchFactor(1, 3)
        root_layout.addWidget(splitter, stretch=3)

        # --- Bottom: log panel ---
        root_layout.addWidget(self._create_log_panel(), stretch=2)

    def _create_toolbar(self) -> QHBoxLayout:
        """Create the top toolbar with COM port controls and status."""
        layout = QHBoxLayout()
        layout.setSpacing(6)

        layout.addWidget(QLabel("串口:"))

        # COM port combo
        self.combo_port = QComboBox()
        self.combo_port.setMinimumWidth(120)
        self.combo_port.activated.connect(self._refresh_ports)
        layout.addWidget(self.combo_port)

        # Refresh button
        self.btn_refresh = QPushButton("刷新")
        self.btn_refresh.setFixedWidth(50)
        self.btn_refresh.clicked.connect(self._refresh_ports)
        layout.addWidget(self.btn_refresh)

        # Connect button
        self.btn_connect = QPushButton("连接")
        self.btn_connect.setFixedWidth(60)
        self.btn_connect.clicked.connect(self._toggle_connection)
        layout.addWidget(self.btn_connect)

        # Separator
        sep = QFrame()
        sep.setFrameShape(QFrame.VLine)
        sep.setFrameShadow(QFrame.Sunken)
        layout.addWidget(sep)

        # Status label
        self.lbl_status = QLabel("未连接")
        self.lbl_status.setStyleSheet("color: #CC0000; font-weight: bold;")
        layout.addWidget(self.lbl_status)

        # Latency label
        self.lbl_latency = QLabel("延迟: --ms")
        self.lbl_latency.setStyleSheet("color: #888888;")
        layout.addWidget(self.lbl_latency)

        layout.addStretch()
        return layout

    def _create_twin_panel(self) -> QWidget:
        """Create the left panel: digital twin display area."""
        panel = QWidget()
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(8)

        # Group box for the twin
        grp = QGroupBox("数字孪生")
        grp_layout = QVBoxLayout(grp)
        grp_layout.setSpacing(8)

        # 7-segment display
        self.seven_seg = SevenSegWidget()
        grp_layout.addWidget(self.seven_seg)

        # LED bar
        self.led_bar = LEDBarWidget()
        grp_layout.addWidget(self.led_bar)

        # Key buttons
        key_grp = QGroupBox("按键模拟")
        key_layout = QGridLayout(key_grp)
        key_layout.setSpacing(2)

        self._key_buttons: dict = {}
        key_names = [
            ("K1", "FUNC"), ("K2", "SHIFT"), ("K3", "ADD"),
            ("K4", "SAVE"), ("K5", "DISP"), ("K6", "SPEED"),
            ("K7", "FORMAT"), ("K8", "EXT"),
        ]
        for i, (label, cmd_name) in enumerate(key_names):
            btn = QPushButton(label)
            btn.setFixedSize(50, 30)
            btn.setToolTip(f"发送按键: {cmd_name}")
            btn.clicked.connect(
                lambda checked, n=cmd_name: self._send_key(n)
            )
            row = i // 5
            col = i % 5
            key_layout.addWidget(btn, row, col)
            self._key_buttons[cmd_name] = btn

        # U1 → NTP sync directly (no board round-trip)
        btn_u1 = QPushButton("U1")
        btn_u1.setFixedSize(50, 30)
        btn_u1.setToolTip("NTP一键对时")
        btn_u1.clicked.connect(self._on_ntp_sync)
        row = 8 // 5; col = 8 % 5
        key_layout.addWidget(btn_u1, row, col)
        self._key_buttons["USER1"] = btn_u1

        # U2 → send weather to board directly
        btn_u2 = QPushButton("U2")
        btn_u2.setFixedSize(50, 30)
        btn_u2.setToolTip("下发天气到数码管")
        btn_u2.clicked.connect(self._send_weather_to_board)
        row = 9 // 5; col = 9 % 5
        key_layout.addWidget(btn_u2, row, col)
        self._key_buttons["USER2"] = btn_u2

        grp_layout.addWidget(key_grp)

        # Night mode indicator
        self.lbl_night = QLabel("模式: DAY")
        self.lbl_night.setStyleSheet("color: #FFAA00; font-weight: bold;")
        self.lbl_night.setAlignment(Qt.AlignCenter)
        grp_layout.addWidget(self.lbl_night)

        layout.addWidget(grp)
        layout.addStretch()
        return panel

    def _create_right_panel(self) -> QWidget:
        """Create the right panel containing the control panel."""
        self.control_panel = ControlPanel()
        return self.control_panel

    def _create_log_panel(self) -> QWidget:
        """Create the bottom log panel area."""
        panel = QWidget()
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)

        grp = QGroupBox("通信日志")
        grp_layout = QVBoxLayout(grp)
        grp_layout.setContentsMargins(2, 2, 2, 2)

        self.log_panel = LogPanel()
        grp_layout.addWidget(self.log_panel)

        layout.addWidget(grp)
        return panel

    # ------------------------------------------------------------------
    # Signal wiring
    # ------------------------------------------------------------------

    def _connect_signals(self) -> None:
        """Connect all signals between subsystems and UI."""

        # Control panel -> send command
        self.control_panel.send_command.connect(self._on_send_command)
        self.control_panel.ntp_sync_requested.connect(self._on_ntp_sync)
        self.control_panel.weather_fetch_requested.connect(self._on_weather_fetch)

        # Heartbeat
        self._heartbeat.timeout.connect(self._on_heartbeat_timeout)
        self._heartbeat.latency_updated.connect(self._on_latency_updated)

        # Twin state -> UI updates
        self._twin_state.seg_changed.connect(self._on_seg_changed)
        self._twin_state.led_changed.connect(self._on_led_changed)
        self._twin_state.mode_changed.connect(self._on_mode_changed)
        self._twin_state.format_changed.connect(self._on_format_changed)
        self._twin_state.alarm_changed.connect(self._on_alarm_changed)
        self._twin_state.key_event.connect(self._on_key_event)
        self._twin_state.edit_event.connect(self._on_edit_event)

    # ------------------------------------------------------------------
    # Connection management
    # ------------------------------------------------------------------

    def _refresh_ports(self) -> None:
        """Refresh the COM port list in the combo box."""
        current = self.combo_port.currentText()
        self.combo_port.clear()

        try:
            ports = SerialWorker.list_ports_with_description()
            for device, desc in ports:
                self.combo_port.addItem(f"{device} - {desc}", device)
        except Exception:
            # Fall back to device-only listing
            try:
                ports = SerialWorker.list_ports()
                for device in ports:
                    self.combo_port.addItem(device, device)
            except Exception:
                pass

        # Restore previous selection if still available
        idx = self.combo_port.findText(current)
        if idx >= 0:
            self.combo_port.setCurrentIndex(idx)

        if self.combo_port.count() == 0:
            self.combo_port.addItem("无可用串口", "")

    def _toggle_connection(self) -> None:
        """Connect or disconnect from the selected serial port."""
        if self._serial_worker and self._serial_worker.is_connected():
            self._disconnect()
        else:
            self._connect()

    def _connect(self) -> None:
        """Establish a serial connection to the selected COM port."""
        device_data = self.combo_port.currentData()
        if not device_data:
            QMessageBox.warning(self, "连接失败", "请选择一个有效的串口。")
            return

        port = device_data

        # Disable connect button while connecting
        self.btn_connect.setEnabled(False)
        self.btn_connect.setText("连接中...")

        self._serial_worker = SerialWorker(
            port=port, baud=DEFAULT_BAUD, timeout=DEFAULT_TIMEOUT
        )
        self._serial_worker.connected.connect(self._on_worker_connected)
        self._serial_worker.received.connect(self._on_worker_received)
        self._serial_worker.error.connect(self._on_worker_error)
        self._serial_worker.finished.connect(self._on_worker_finished)
        self._serial_worker.start()

    def _disconnect(self) -> None:
        """Disconnect from the serial port."""
        if self._serial_worker:
            self._serial_worker.stop()
            # Worker will emit finished -> _on_worker_finished handles cleanup
        self._heartbeat.stop()
        self._update_connection_ui(False, "已断开")

    def _on_worker_connected(self, success: bool, message: str) -> None:
        """Handle SerialWorker connection result."""
        self.btn_connect.setEnabled(True)

        if success:
            self.btn_connect.setText("断开")
            self._update_connection_ui(True, f"已连接: {message}")
            self._twin_state.set_connected(True)
            self._heartbeat.start()
            self._update_weather_button_state()
            self.log_panel.addEntry("OK", f"串口 {message} 连接成功")
        else:
            self.btn_connect.setText("连接")
            self._update_connection_ui(False, f"连接失败: {message}")
            self._twin_state.set_connected(False)
            self.log_panel.addError(f"连接失败: {message}")
            QMessageBox.critical(self, "连接失败", message)

    def _on_worker_received(self, data: bytes) -> None:
        """Handle raw data received from the serial port."""
        # Log raw RX
        try:
            raw_text = data.decode("ascii", errors="replace").strip()
            if raw_text:
                self.log_panel.addEntry("RX", raw_text)
        except Exception:
            pass

        # Parse frames
        frames = self._parser.parse_incoming(data)
        for frame in frames:
            ftype = frame.get("type", "unknown")

            # Log events separately
            if ftype.startswith("evt_"):
                self.log_panel.addEntry("EVT", frame.get("raw", ""))
            elif ftype == "error":
                self.log_panel.addEntry("ERR", frame.get("raw", ""))

            # Feed heartbeat on display or LED events
            if self._parser.is_heartbeat_frame(frame):
                self._heartbeat.feed()

            # Handle pong for latency
            if ftype == "pong":
                self._heartbeat.record_pong_received()

            # Track NTP OK responses
            if self._ntp_pending and ftype == "ok":
                self._ntp_ok_count += 1
            elif self._ntp_pending and ftype == "error":
                self.log_panel.addEntry("ERR", f"NTP命令返回错误: {frame.get('raw', '')}")

            # Process into twin state
            self._twin_state.process_frame(frame)

    def _on_worker_error(self, message: str) -> None:
        """Handle serial worker error."""
        self.log_panel.addError(message)

    def _on_worker_finished(self) -> None:
        """Handle serial worker thread finishing."""
        self.btn_connect.setText("连接")
        self.btn_connect.setEnabled(True)
        self._heartbeat.stop()
        self._update_connection_ui(False, "未连接")
        self._twin_state.set_connected(False)
        self._twin_state.reset()
        # Reset NTP state
        self._ntp_pending = False
        self._ntp_ok_count = 0

    def _on_send_command(self, cmd: str) -> None:
        """Send a command string over the serial port.

        Args:
            cmd: Formatted command string (should include line ending).
        """
        if not self._serial_worker or not self._serial_worker.is_connected():
            self.log_panel.addError("未连接，无法发送命令")
            return

        # Log TX
        stripped = cmd.strip()
        self.log_panel.addEntry("TX", stripped)

        # Track ping for latency
        if stripped.upper().startswith("*PING"):
            self._heartbeat.record_ping_sent()

        # Send as ASCII bytes
        try:
            self._serial_worker.send(cmd.encode("ascii"))
        except Exception as e:
            self.log_panel.addError(f"发送失败: {e}")

    def _send_key(self, key_name: str) -> None:
        """Send a SET:KEY command for simulated key press.

        Args:
            key_name: Key name (FUNC, SHIFT, ADD, SAVE, DISP, SPEED,
                      FORMAT, EXT, USER1, USER2).
        """
        from .protocol import ProtocolParser
        cmd = ProtocolParser.format_command("SET", "KEY", [key_name])
        self._on_send_command(cmd + "\r\n")

    # ------------------------------------------------------------------
    # E1: NTP Time Synchronization
    # ------------------------------------------------------------------

    def _on_ntp_sync(self) -> None:
        """Handle NTP sync button click (E1).

        Fetches network time, sends *SET:DATE and *SET:TIME commands,
        and controls LED D6 to indicate success/failure.
        """
        if not self._serial_worker or not self._serial_worker.is_connected():
            QMessageBox.warning(self, "NTP 对时", "请先连接串口。")
            return

        if self._ntp_pending:
            return  # Already in progress

        # Disable button during request
        self.control_panel.set_ntp_enabled(False)
        self.lbl_status.setText("NTP 对时中...")
        self.lbl_status.setStyleSheet("color: #FFAA00; font-weight: bold;")

        # Perform NTP request (may block briefly - use QTimer to keep UI responsive)
        result = self._ntp_client.fetch_time()

        if not result.success:
            # NTP request failed
            self.control_panel.set_ntp_enabled(True)
            self._update_connection_ui(
                self._serial_worker and self._serial_worker.is_connected(),
                "NTP 失败"
            )
            self.log_panel.addError(f"NTP对时失败: {result.error_msg}")
            QMessageBox.warning(self, "NTP 对时失败", result.error_msg)
            return

        # NTP succeeded — send DATE and TIME commands
        self._ntp_pending = True
        self._ntp_ok_count = 0

        date_cmd = self._ntp_client.get_date_command(result)
        time_cmd = self._ntp_client.get_time_command(result)

        self.log_panel.addEntry("TX", f"NTP对时: {date_cmd.strip()}")
        self._on_send_command(date_cmd)

        self.log_panel.addEntry("TX", f"NTP对时: {time_cmd.strip()}")
        self._on_send_command(time_cmd)

        # Schedule result check in 1.5s (allow time for both responses)
        self._ntp_check_timer = QTimer(self)
        self._ntp_check_timer.setSingleShot(True)
        self._ntp_check_timer.timeout.connect(self._on_ntp_check_responses)
        self._ntp_check_timer.start(1500)

    def _on_ntp_check_responses(self) -> None:
        """Check NTP command responses and control LED D6 accordingly."""
        self._ntp_pending = False

        if self._ntp_ok_count >= 2:
            # Both DATE and TIME succeeded → light D6
            self.log_panel.addEntry("OK", "NTP对时成功 ✓")
            self.lbl_status.setText("NTP ✓")
            self.lbl_status.setStyleSheet(
                "color: #00AA00; font-weight: bold;"
            )

            # Send LED command to light D6
            led_cmd = f"*SET:LED {NTP_LED_D6_MASK:02X}\r\n"
            self.log_panel.addEntry("TX", f"NTP LED D6: {led_cmd.strip()}")
            if self._serial_worker and self._serial_worker.is_connected():
                try:
                    self._serial_worker.send(led_cmd.encode("ascii"))
                except Exception:
                    pass

            # Schedule LED D6 off after duration
            QTimer.singleShot(NTP_LED_DURATION_MS, self._on_ntp_led_off)

            # Schedule status label reset
            QTimer.singleShot(NTP_STATUS_DISPLAY_MS, self._on_ntp_status_reset)
        else:
            # Not enough OK responses
            self.log_panel.addError(
                f"NTP对时: 板端响应不足 (收到{self._ntp_ok_count}个OK，预期2个)"
            )
            self._on_ntp_status_reset()

        # Re-enable NTP button
        self.control_panel.set_ntp_enabled(True)

    def _on_ntp_led_off(self) -> None:
        """Turn off LED D6 after NTP success indication duration."""
        led_cmd = "*SET:LED 00\r\n"
        if self._serial_worker and self._serial_worker.is_connected():
            try:
                self._serial_worker.send(led_cmd.encode("ascii"))
            except Exception:
                pass

    def _on_ntp_status_reset(self) -> None:
        """Reset status label after NTP success display expires."""
        self._update_status_text()

    # ------------------------------------------------------------------
    # E2: Weather Fetch and USER2 Response
    # ------------------------------------------------------------------

    def _on_weather_fetch(self) -> None:
        """Handle weather fetch button click (E2).

        Fetches weather data from the configured API and updates the cache.
        """
        # Disable button during fetch
        self.control_panel.set_weather_enabled(False)
        self.lbl_status.setText("获取天气中...")
        self.lbl_status.setStyleSheet("color: #FFAA00; font-weight: bold;")

        result = self._weather_client.fetch()

        if result.success:
            self.log_panel.addEntry(
                "OK",
                f"天气获取成功: {result.display_text}"
            )
            self.control_panel.set_weather_age(
                self._weather_client.cache.age_text()
            )
            # Auto-send to board so user sees weather immediately
            self._send_weather_to_board()
        else:
            self.log_panel.addError(
                f"天气获取失败: {result.error_msg}"
            )
            QMessageBox.warning(self, "天气获取失败", result.error_msg)
            # Keep old cache age if available
            if self._weather_client.cache.result is not None:
                self.control_panel.set_weather_age(
                    self._weather_client.cache.age_text() + " (过期)"
                )

        # Re-enable button and restore status
        self.control_panel.set_weather_enabled(True)
        self._update_status_text()

    def _send_weather_to_board(self) -> None:
        """Send cached weather data to the S800 board via *SET:MSG,
        then restore clock display after 5 seconds."""
        if not self._serial_worker or not self._serial_worker.is_connected():
            return

        display_text = self._weather_client.get_display_text()
        cmd = f"*SET:MSG {display_text}\r\n"
        self.log_panel.addEntry("TX", f"天气下发: {cmd.strip()}")
        try:
            self._serial_worker.send(cmd.encode("ascii"))
        except Exception as e:
            self.log_panel.addError(f"天气下发失败: {e}")

        # Auto-revert to clock after 5 seconds
        QTimer.singleShot(5000, self._revert_to_clock)

    def _revert_to_clock(self) -> None:
        """Send *SET:DISP TIME to restore clock display."""
        if not self._serial_worker or not self._serial_worker.is_connected():
            return
        cmd = "*SET:DISP TIME\r\n"
        self.log_panel.addEntry("TX", "天气超时,恢复时钟")
        try:
            self._serial_worker.send(cmd.encode("ascii"))
        except Exception:
            pass

    def _update_weather_button_state(self) -> None:
        """Update weather button state based on API key availability."""
        if self._weather_client.has_api_key:
            self.control_panel.set_weather_enabled(True)
            self.control_panel.btn_weather.setToolTip("从天气API获取实时天气数据")
        else:
            self.control_panel.set_weather_enabled(False)
            self.control_panel.btn_weather.setToolTip(
                "未配置 WEATHER_API_KEY，请在 .env 中设置后重启程序"
            )

    # ------------------------------------------------------------------
    # Heartbeat handlers
    # ------------------------------------------------------------------

    def _on_heartbeat_timeout(self) -> None:
        """Handle heartbeat timeout (connection lost)."""
        self._update_connection_ui(False, "连接超时")
        self._twin_state.set_connected(False)
        self.log_panel.addError("心跳超时: 与设备失去连接")
        if self._serial_worker:
            self._serial_worker.stop()

    def _on_latency_updated(self, latency_ms: int) -> None:
        """Update the latency display label.

        Args:
            latency_ms: Round-trip latency in milliseconds.
        """
        self.lbl_latency.setText(f"延迟: {latency_ms}ms")
        if latency_ms < 50:
            self.lbl_latency.setStyleSheet("color: #00AA00;")
        elif latency_ms < 200:
            self.lbl_latency.setStyleSheet("color: #FFAA00;")
        else:
            self.lbl_latency.setStyleSheet("color: #CC0000;")

    # ------------------------------------------------------------------
    # Twin state -> UI handlers
    # ------------------------------------------------------------------

    def _on_seg_changed(self, text: str, dp_hex: int) -> None:
        """Update the 7-segment display widget."""
        self.seven_seg.setText(text)
        self.seven_seg.setDp(dp_hex)

    def _on_led_changed(self, byte_val: int) -> None:
        """Update the LED bar widget."""
        self.led_bar.setValue(byte_val)

    def _on_mode_changed(self, mode: str) -> None:
        """Update night mode status and display."""
        night = mode.upper() == "NIGHT"
        self.seven_seg.setNightMode(night)
        self.lbl_night.setText(f"模式: {mode.upper()}")
        if night:
            self.lbl_night.setStyleSheet(
                "color: #4444FF; font-weight: bold;"
            )
        else:
            self.lbl_night.setStyleSheet(
                "color: #FFAA00; font-weight: bold;"
            )

    def _on_format_changed(self, fmt: str) -> None:
        """Update format display in status."""
        self._update_status_text()

    def _on_alarm_changed(self, enabled: bool) -> None:
        """Update alarm status in the status label."""
        self._update_status_text()

    def _on_key_event(self, key_name: str) -> None:
        """Handle key events from the board.

        USER1 triggers NTP time sync (E1).
        USER2 triggers automatic weather data delivery (E2).
        """
        if key_name == "USER1":
            self.log_panel.addEntry("EVT", "USER1: 板端请求NTP对时")
            self._on_ntp_sync()
        elif key_name == "USER2":
            self.log_panel.addEntry("EVT", "USER2: 板端请求天气数据")
            self._send_weather_to_board()

    def _on_edit_event(self, edit_type: str, edit_value: str) -> None:
        """Log edit events."""
        pass  # Edit events reflected in display state

    # ------------------------------------------------------------------
    # UI helpers
    # ------------------------------------------------------------------

    def _update_connection_ui(self, connected: bool, status_text: str) -> None:
        """Update status label and connection button.

        Args:
            connected: Whether the connection is active.
            status_text: Status text to display.
        """
        self.lbl_status.setText(status_text)
        if connected:
            self.lbl_status.setStyleSheet(
                "color: #009933; font-weight: bold;"
            )
            self.control_panel.set_ntp_enabled(True)
        else:
            self.lbl_status.setStyleSheet(
                "color: #CC0000; font-weight: bold;"
            )
            self.lbl_latency.setText("延迟: --ms")
            self.lbl_latency.setStyleSheet("color: #888888;")
            self.control_panel.set_ntp_enabled(False)

    def _update_status_text(self) -> None:
        """Refresh the status label with current format and alarm state."""
        if self._twin_state.is_connected():
            fmt = self._twin_state.get_format()
            alarm = "闹钟ON" if self._twin_state.is_alarm_active() else "闹钟OFF"
            port = self.combo_port.currentData() or "?"
            self.lbl_status.setText(
                f"已连接: {port} | 格式:{fmt} | {alarm}"
            )
            self.lbl_status.setStyleSheet(
                "color: #009933; font-weight: bold;"
            )

    # ------------------------------------------------------------------
    # Window lifecycle
    # ------------------------------------------------------------------

    def closeEvent(self, event) -> None:
        """Handle window close: stop serial worker and heartbeat."""
        self._heartbeat.stop()
        self._ntp_pending = False  # cancel any pending NTP
        if self._ntp_check_timer is not None:
            self._ntp_check_timer.stop()
        if self._serial_worker:
            self._serial_worker.stop()
            self._serial_worker.wait(2000)  # Wait up to 2s for thread to finish
        event.accept()
