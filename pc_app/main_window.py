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
)
from .heartbeat import HeartbeatMonitor
from .protocol import ProtocolParser
from .serial_worker import SerialWorker
from .twin_state import TwinStateManager
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
            ("K7", "FORMAT"), ("K8", "EXT"), ("U1", "USER1"),
            ("U2", "USER2"),
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
        """Log key events (UI state already handled by display)."""
        pass  # Key events are displayed via seg/led changes

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
        else:
            self.lbl_status.setStyleSheet(
                "color: #CC0000; font-weight: bold;"
            )
            self.lbl_latency.setText("延迟: --ms")
            self.lbl_latency.setStyleSheet("color: #888888;")

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
        if self._serial_worker:
            self._serial_worker.stop()
            self._serial_worker.wait(2000)  # Wait up to 2s for thread to finish
        event.accept()
