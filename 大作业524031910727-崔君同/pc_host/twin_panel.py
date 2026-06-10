"""Digital twin state manager.

Maintains a mirror of the S800 board's state and emits Qt signals
when that state changes, allowing the GUI to stay in sync.
"""

from __future__ import annotations

from typing import Any, Dict

from PyQt5.QtCore import QObject, pyqtSignal


class TwinStateManager(QObject):
    """Manages the digital twin state mirroring the S800 board.

    Processes parsed protocol frames, updates internal state, and
    emits signals so GUI widgets can react to changes.
    """

    # Signals
    seg_changed = pyqtSignal(str, int)     # 8-char string, dp_hex byte
    led_changed = pyqtSignal(int)          # 8-bit LED value (0-255)
    mode_changed = pyqtSignal(str)         # "DAY" or "NIGHT"
    format_changed = pyqtSignal(str)       # "LEFT" or "RIGHT"
    alarm_changed = pyqtSignal(bool)       # True = enabled/ringing
    key_event = pyqtSignal(str)            # Key name from EVT:KEY
    edit_event = pyqtSignal(str, str)      # Type, Value from EVT:EDIT
    pong_received = pyqtSignal(int)        # Uptime in seconds

    def __init__(self, parent: QObject = None) -> None:
        """Initialize the twin state manager with default values."""
        super().__init__(parent)
        self._seg_text: str = "        "  # 8 spaces
        self._dp_hex: int = 0
        self._led_byte: int = 0
        self._mode: str = "DAY"
        self._format: str = "LEFT"
        self._alarm: bool = False
        self._connected: bool = False

    def process_frame(self, frame: Dict[str, Any]) -> None:
        """Process a parsed protocol frame and update state.

        Updates internal state and emits the appropriate signals so
        the GUI can react. Handles all known frame types.

        Args:
            frame: A dict from ProtocolParser.parse_incoming/parse_response
                   with at minimum 'type' and 'raw' keys.
        """
        ftype = frame.get("type", "unknown")

        if ftype == "evt_disp":
            self._seg_text = frame.get("seg_text", self._seg_text)
            self._dp_hex = frame.get("dp_hex", self._dp_hex)
            self.seg_changed.emit(self._seg_text, self._dp_hex)

        elif ftype == "evt_led":
            self._led_byte = frame.get("led_byte", self._led_byte)
            self.led_changed.emit(self._led_byte)

        elif ftype == "evt_mode":
            self._mode = frame.get("mode_state", self._mode)
            self.mode_changed.emit(self._mode)

        elif ftype == "evt_key":
            key_name = frame.get("key_name", "?")
            self.key_event.emit(key_name)

        elif ftype == "evt_edit":
            edit_type = frame.get("edit_type", "?")
            edit_value = frame.get("edit_value", "")
            self.edit_event.emit(edit_type, edit_value)

        elif ftype == "evt_alarm":
            self._alarm = frame.get("enabled", False)
            self.alarm_changed.emit(self._alarm)

        elif ftype == "pong":
            uptime = frame.get("uptime_s", 0)
            self.pong_received.emit(uptime)

        elif ftype == "error":
            # Errors are handled at the log level, not state
            pass

        elif ftype == "ok":
            data = frame.get("data", "").upper()
            if data == "LEFT" or data == "RIGHT":
                self._format = data
                self.format_changed.emit(data)
            elif data == "OFF":
                self._alarm = False
                self.alarm_changed.emit(False)
            elif " " in data and "." in data:
                # alarm response "HH.MM.SS D" has space + dots
                self._alarm = True
                self.alarm_changed.emit(True)

    # --- Accessors ---

    def get_seg_text(self) -> str:
        """Return the current 8-character display text."""
        return self._seg_text

    def get_dp_hex(self) -> int:
        """Return the current decimal point mask byte."""
        return self._dp_hex

    def get_led_byte(self) -> int:
        """Return the current LED byte value."""
        return self._led_byte

    def get_mode(self) -> str:
        """Return the current mode ('DAY' or 'NIGHT')."""
        return self._mode

    def get_format(self) -> str:
        """Return the current display format ('LEFT' or 'RIGHT')."""
        return self._format

    def is_alarm_active(self) -> bool:
        """Return True if the alarm is currently active/enabled."""
        return self._alarm

    def is_connected(self) -> bool:
        """Return True if the board connection is alive."""
        return self._connected

    def set_connected(self, state: bool) -> None:
        """Update the connection state (used by heartbeat monitor)."""
        self._connected = state

    def reset(self) -> None:
        """Reset all state to defaults."""
        self._seg_text = "        "
        self._dp_hex = 0
        self._led_byte = 0
        self._mode = "DAY"
        self._format = "LEFT"
        self._alarm = False
        self._connected = False
