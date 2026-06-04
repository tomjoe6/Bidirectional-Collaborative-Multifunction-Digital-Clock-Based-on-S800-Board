"""Transmit/receive log panel widget.

Displays a color-coded log of serial communication with the S800 board.
Supports export to TXT and CSV formats.
"""

from __future__ import annotations

import csv
from typing import Optional

from PyQt5.QtCore import Qt, QTime
from PyQt5.QtGui import QColor, QTextCharFormat, QTextCursor
from PyQt5.QtWidgets import (
    QHBoxLayout,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from ..config import (
    COLOR_ERR,
    COLOR_EVT,
    COLOR_OK,
    COLOR_RX,
    COLOR_TX,
    LOG_MAX_LINES,
)


class LogPanel(QWidget):
    """Color-coded log panel for serial communication.

    Displays transmitted commands, received responses, events,
    and errors with distinct colors and timestamps.
    """

    def __init__(self, parent: QWidget = None) -> None:
        """Initialize the log panel.

        Args:
            parent: Optional parent widget.
        """
        super().__init__(parent)
        self._entries: list = []  # stores (direction, content, timestamp) tuples
        self._max_lines: int = LOG_MAX_LINES

        self._setup_ui()

    def _setup_ui(self) -> None:
        """Build the log panel layout."""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)

        # Log text area
        self._text_edit = QPlainTextEdit()
        self._text_edit.setReadOnly(True)
        self._text_edit.setMaximumBlockCount(self._max_lines)
        self._text_edit.setStyleSheet(
            "QPlainTextEdit { background-color: #1E1E1E; color: #D4D4D4; "
            "font-family: 'Consolas', 'Courier New', monospace; font-size: 12px; }"
        )
        layout.addWidget(self._text_edit)

        # Button row
        btn_layout = QHBoxLayout()
        btn_layout.setSpacing(4)

        self._btn_clear = QPushButton("清空日志")
        self._btn_clear.clicked.connect(self.clear)
        btn_layout.addWidget(self._btn_clear)

        self._btn_txt = QPushButton("导出 TXT")
        self._btn_txt.clicked.connect(self._export_txt)
        btn_layout.addWidget(self._btn_txt)

        self._btn_csv = QPushButton("导出 CSV")
        self._btn_csv.clicked.connect(self._export_csv)
        btn_layout.addWidget(self._btn_csv)

        btn_layout.addStretch()
        layout.addLayout(btn_layout)

    def addEntry(self, direction: str, content: str,
                 timestamp: Optional[QTime] = None) -> None:
        """Add a log entry with color-coded direction.

        Args:
            direction: One of "TX", "RX", "EVT", "ERR".
            content: The log message content.
            timestamp: Optional QTime; uses current time if None.
        """
        if timestamp is None:
            timestamp = QTime.currentTime()

        self._entries.append((direction, content, timestamp))

        # Enforce max lines (trim from the front)
        while len(self._entries) > self._max_lines:
            self._entries.pop(0)

        # Build formatted line
        ts_str = timestamp.toString("HH:mm:ss.zzz")

        # Color map
        color_map = {
            "TX": COLOR_TX,
            "RX": COLOR_RX,
            "EVT": COLOR_EVT,
            "ERR": COLOR_ERR,
            "OK": COLOR_OK,
        }
        color = color_map.get(direction, "#FFFFFF")

        # Arrow/indicator
        arrow_map = {
            "TX": "TX →",
            "RX": "RX ←",
            "EVT": "EVT ◊",
            "ERR": "ERR ✖",
            "OK": "OK  ✓",
        }
        arrow = arrow_map.get(direction, direction)

        line = f"[{ts_str}] {arrow} {content}"

        # Use HTML for colored text in QPlainTextEdit-compatible way
        # QPlainTextEdit supports limited rich text via appendHtml
        html_line = (
            f'<span style="color:{color};">{self._escape_html(line)}</span>'
        )
        self._text_edit.appendHtml(html_line)

        # Auto-scroll to bottom
        self._text_edit.moveCursor(QTextCursor.End)

    def addError(self, content: str) -> None:
        """Add an error entry (convenience method).

        Args:
            content: Error message.
        """
        self.addEntry("ERR", content)

    def clear(self) -> None:
        """Clear all log entries."""
        self._entries.clear()
        self._text_edit.clear()

    def _export_txt(self) -> None:
        """Export log to a .txt file."""
        from PyQt5.QtWidgets import QFileDialog

        path, _ = QFileDialog.getSaveFileName(
            self, "导出日志 (TXT)", "", "Text Files (*.txt);;All Files (*)"
        )
        if path:
            self.exportTxt(path)

    def exportTxt(self, path: str) -> None:
        """Export log entries to a plain text file.

        Args:
            path: Destination file path.
        """
        try:
            with open(path, "w", encoding="utf-8") as f:
                for direction, content, timestamp in self._entries:
                    ts_str = timestamp.toString("HH:mm:ss.zzz")
                    f.write(f"[{ts_str}] {direction} {content}\n")
        except IOError as e:
            self.addError(f"导出 TXT 失败: {e}")

    def _export_csv(self) -> None:
        """Export log to a .csv file."""
        from PyQt5.QtWidgets import QFileDialog

        path, _ = QFileDialog.getSaveFileName(
            self, "导出日志 (CSV)", "", "CSV Files (*.csv);;All Files (*)"
        )
        if path:
            self.exportCsv(path)

    def exportCsv(self, path: str) -> None:
        """Export log entries to a CSV file.

        Args:
            path: Destination file path.
        """
        try:
            with open(path, "w", encoding="utf-8", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(["Timestamp", "Direction", "Content"])
                for direction, content, timestamp in self._entries:
                    ts_str = timestamp.toString("HH:mm:ss.zzz")
                    writer.writerow([ts_str, direction, content])
        except IOError as e:
            self.addError(f"导出 CSV 失败: {e}")

    @staticmethod
    def _escape_html(text: str) -> str:
        """Escape HTML special characters in text."""
        return (
            text.replace("&", "&amp;")
            .replace("<", "&lt;")
            .replace(">", "&gt;")
            .replace('"', "&quot;")
        )
