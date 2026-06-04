"""LED indicator widgets for the S800 digital twin.

Provides single LED indicator and an 8-LED bar widget.
"""

from __future__ import annotations

from typing import List, Tuple

from PyQt5.QtCore import Qt, QSize
from PyQt5.QtGui import QBrush, QColor, QPainter, QPen
from PyQt5.QtWidgets import QHBoxLayout, QLabel, QSizePolicy, QVBoxLayout, QWidget


class LEDIndicator(QWidget):
    """A single circular LED indicator.

    Can be toggled between ON (bright color) and OFF (dark gray) states.
    """

    def __init__(self, parent: QWidget = None, size: int = 24) -> None:
        """Initialize the LED indicator.

        Args:
            parent: Optional parent widget.
            size: Diameter of the LED in pixels.
        """
        super().__init__(parent)
        self._size: int = size
        self._on: bool = False
        self._on_color: QColor = QColor("#00FF00")  # Default green
        self._off_color: QColor = QColor("#333333")  # Dark gray

        self.setFixedSize(size + 4, size + 4)
        self.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

    def setColor(self, on_color: QColor) -> None:
        """Set the color used when the LED is ON.

        Args:
            on_color: QColor for the ON state.
        """
        self._on_color = QColor(on_color)
        self.update()

    def setState(self, on: bool) -> None:
        """Set the LED state.

        Args:
            on: True to light the LED, False to turn it off.
        """
        self._on = on
        self.update()

    def isOn(self) -> bool:
        """Return True if the LED is currently ON."""
        return self._on

    def paintEvent(self, event) -> None:
        """Paint the circular LED indicator."""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        color = self._on_color if self._on else self._off_color

        # Outer glow (subtle ring)
        if self._on:
            glow_color = QColor(color)
            glow_color.setAlpha(80)
            painter.setPen(Qt.NoPen)
            painter.setBrush(QBrush(glow_color))
            glow_r = self._size // 2 + 2
            glow_cx = self.width() // 2
            glow_cy = self.height() // 2
            painter.drawEllipse(glow_cx - glow_r, glow_cy - glow_r,
                                glow_r * 2, glow_r * 2)

        # Main LED circle
        painter.setPen(QPen(color.darker(120), 1))
        painter.setBrush(QBrush(color))
        r = self._size // 2
        cx = self.width() // 2
        cy = self.height() // 2
        painter.drawEllipse(cx - r, cy - r, self._size, self._size)

        # Highlight (specular reflection)
        if self._on:
            highlight_color = QColor(255, 255, 255, 100)
            painter.setPen(Qt.NoPen)
            painter.setBrush(QBrush(highlight_color))
            painter.drawEllipse(cx - r // 2, cy - r // 2, r * 2 // 3, r * 2 // 3)

        painter.end()


class LEDBarWidget(QWidget):
    """A horizontal bar of 8 LED indicators with function labels.

    Each LED corresponds to one bit of a byte value (0-255).
    LEDs are labeled D0 through D7 with function descriptions.
    """

    # Default function labels for each LED
    DEFAULT_LABELS: List[str] = [
        "D0:心跳",
        "D1:闹钟使能",
        "D2:设置",
        "D3:编辑",
        "D4:按键",
        "D5:串口",
        "D6:保留",
        "D7:保留",
    ]

    def __init__(self, parent: QWidget = None,
                 labels: List[str] = None) -> None:
        """Initialize the 8-LED bar widget.

        Args:
            parent: Optional parent widget.
            labels: Custom labels for each LED (defaults to DEFAULT_LABELS).
        """
        super().__init__(parent)
        self._labels = labels if labels is not None else self.DEFAULT_LABELS
        self._leds: List[LEDIndicator] = []
        self._value: int = 0

        self._setup_ui()

    def _setup_ui(self) -> None:
        """Create the LED bar layout."""
        main_layout = QVBoxLayout(self)
        main_layout.setSpacing(2)
        main_layout.setContentsMargins(4, 4, 4, 4)

        # LED row
        led_layout = QHBoxLayout()
        led_layout.setSpacing(4)

        for i in range(8):
            led = LEDIndicator(self, size=20)
            led.setColor(QColor("#00CC00"))  # Green LEDs
            self._leds.append(led)
            led_layout.addWidget(led)

        led_layout.addStretch()
        main_layout.addLayout(led_layout)

        # Label row
        label_layout = QHBoxLayout()
        label_layout.setSpacing(4)

        for i in range(8):
            label = QLabel(self._labels[i] if i < len(self._labels) else f"D{i}")
            label.setAlignment(Qt.AlignCenter)
            label.setStyleSheet("color: #CCCCCC; font-size: 9px;")
            label.setFixedWidth(28)
            label_layout.addWidget(label)

        label_layout.addStretch()
        main_layout.addLayout(label_layout)

    def setValue(self, byte: int) -> None:
        """Set all 8 LEDs from a byte value.

        Each bit corresponds to one LED: bit 0 = D0, bit 1 = D1, etc.

        Args:
            byte: Integer 0-255 representing the LED states.
        """
        self._value = byte & 0xFF
        for i in range(8):
            self._leds[i].setState((self._value >> i) & 1)

    def value(self) -> int:
        """Return the current byte value."""
        return self._value

    def setLabels(self, labels: List[str]) -> None:
        """Update the function labels below each LED.

        Args:
            labels: List of 8 label strings.
        """
        self._labels = labels
        # Labels are set during init; layout children would need updating
        # For dynamic updates, re-create layout. Simplified for now.
