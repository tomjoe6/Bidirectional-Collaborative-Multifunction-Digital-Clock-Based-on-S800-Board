"""Custom 7-segment display widget for the S800 digital twin.

Renders 8 7-segment digits with decimal points using QPainter.
Segment shapes are trapezoids for a realistic LED display appearance.
"""

from __future__ import annotations

from typing import List, Tuple

from PyQt5.QtCore import Qt, QRectF, QPointF
from PyQt5.QtGui import QBrush, QColor, QPainter, QPen, QPolygonF
from PyQt5.QtWidgets import QSizePolicy, QWidget

# Segment "on" and "off" colors
COLOR_ON = QColor("#FF0000")    # Bright red
COLOR_OFF = QColor("#330000")   # Very dark red (barely visible)
COLOR_NIGHT_OFF = QColor("#110000")  # Even darker for night mode unlit digits

# Segment names a-g, standard 7-segment layout
#    a
# f     b
#    g
# e     c
#    d   (dp)

# Segment encoding: each segment is a polygon defined as offsets
# relative to a digit's origin. Dimensions are based on a digit cell
# of width=48, height=72 (used for coordinate generation).

DIGIT_W = 48
DIGIT_H = 72
DIGIT_SPACING = 56
SEGMENT_W = 6
DP_RADIUS = 3

# Number of digits in the display
NUM_DIGITS = 8

# Segment definitions as polygon points (relative to digit origin 0,0)
# Each tuple: list of (x, y) points forming the segment polygon
# The origin (0,0) is the top-left of the digit cell (DIGIT_W x DIGIT_H)


def _make_segment_polygons() -> List[List[Tuple[float, float]]]:
    """Generate the 7 segment polygon definitions."""
    w = DIGIT_W
    h = DIGIT_H
    sw = SEGMENT_W  # segment thickness
    m = 2  # margin from edge

    # Helper: a horizontal trapezoid centered at (cx, cy), length L
    def h_seg(cx: float, cy: float, length: float) -> List[Tuple[float, float]]:
        hl = length / 2.0
        hsw = sw / 2.0
        return [
            (cx - hl + hsw, cy - hsw),
            (cx + hl - hsw, cy - hsw),
            (cx + hl, cy),
            (cx + hl - hsw, cy + hsw),
            (cx - hl + hsw, cy + hsw),
            (cx - hl, cy),
        ]

    # Helper: a vertical trapezoid centered at (cx, cy), length L
    def v_seg(cx: float, cy: float, length: float) -> List[Tuple[float, float]]:
        hl = length / 2.0
        hsw = sw / 2.0
        return [
            (cx - hsw, cy - hl + hsw),
            (cx + hsw, cy - hl + hsw),
            (cx + hsw, cy + hl - hsw),
            (cx, cy + hl),
            (cx - hsw, cy + hl - hsw),
            (cx, cy - hl),
        ]

    cx = w / 2.0
    cy = h / 2.0
    h_len = w - 2 * m - sw       # horizontal segment length
    v_len = (h - 3 * sw - 2 * m) / 2.0  # vertical segment length

    top_y = m + sw / 2.0
    bot_y = h - m - sw / 2.0
    mid_y = h / 2.0

    segments = [
        h_seg(cx, top_y, h_len),                # a (top)
        v_seg(w - m - sw / 2.0, top_y + v_len / 2.0 + sw / 2.0, v_len),  # b (top-right)
        v_seg(w - m - sw / 2.0, bot_y - v_len / 2.0 - sw / 2.0, v_len),  # c (bottom-right)
        h_seg(cx, bot_y, h_len),                # d (bottom)
        v_seg(m + sw / 2.0, bot_y - v_len / 2.0 - sw / 2.0, v_len),      # e (bottom-left)
        v_seg(m + sw / 2.0, top_y + v_len / 2.0 + sw / 2.0, v_len),      # f (top-left)
        h_seg(cx, mid_y, h_len),                # g (middle)
    ]
    return segments


SEGMENT_POLYGONS = _make_segment_polygons()

# 7-segment truth table: which segments are lit for each character
# Indexed by character; segments a-g (LSB = a, MSB = g)
# Segment bit mapping: bit0=a, bit1=b, bit2=c, bit3=d, bit4=e, bit5=f, bit6=g
# (matches firmware g_seg_table)
SEGMENT_MAP: dict = {
    '0': 0x3F, '1': 0x06, '2': 0x5B, '3': 0x4F, '4': 0x66,
    '5': 0x6D, '6': 0x7D, '7': 0x07, '8': 0x7F, '9': 0x6F,
    'A': 0x77, 'B': 0x7C, 'C': 0x39, 'D': 0x5E, 'E': 0x79,
    'F': 0x71, 'G': 0x3D, 'H': 0x76, 'I': 0x30, 'J': 0x1E,
    'K': 0x7A, 'L': 0x38, 'M': 0x55, 'N': 0x37, 'O': 0x3F,
    'P': 0x73, 'Q': 0x67, 'R': 0x70, 'S': 0x6D, 'T': 0x78,
    'U': 0x3E, 'V': 0x7E, 'W': 0x6A, 'X': 0x36, 'Y': 0x6E,
    'Z': 0x5B,
    '-': 0x40, '_': 0x08, ' ': 0x00, '.': 0x80,
}

# Map for lowercase
for _ch in list(SEGMENT_MAP.keys()):
    if _ch.isalpha():
        SEGMENT_MAP[_ch.lower()] = SEGMENT_MAP[_ch]


class SevenSegWidget(QWidget):
    """Custom widget rendering 8 7-segment digits with decimal points.

    Paints realistic LED-style 7-segment digits using QPainter.
    Supports night mode where only the first 4 digits are lit.
    """

    def __init__(self, parent: QWidget = None) -> None:
        """Initialize the 7-segment display widget.

        Args:
            parent: Optional parent widget.
        """
        super().__init__(parent)
        self._text: str = "        "  # 8 characters
        self._dp_hex: int = 0          # bitmask for decimal points
        self._night_mode: bool = False

        self.setMinimumSize(DIGIT_SPACING * NUM_DIGITS, DIGIT_H + 10)
        self.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

    def sizeHint(self):
        """Return the preferred size of the widget."""
        from PyQt5.QtCore import QSize
        return QSize(DIGIT_SPACING * NUM_DIGITS, DIGIT_H + 10)

    def setText(self, text: str) -> None:
        """Set the 8-character display text.

        Args:
            text: Up to 8 characters to display. Padded/truncated to 8.
        """
        text = str(text)[:NUM_DIGITS]
        self._text = text.ljust(NUM_DIGITS)
        self.update()

    def setDp(self, dp_hex: int) -> None:
        """Set the decimal point mask.

        Args:
            dp_hex: Byte where bit 0 corresponds to digit 0's DP, etc.
        """
        self._dp_hex = dp_hex & 0xFF
        self.update()

    def setNightMode(self, night: bool) -> None:
        """Enable or disable night mode.

        In night mode, only the first 4 digits (HH.MM) are illuminated;
        the remaining 4 digits are dark.

        Args:
            night: True for night mode, False for day mode.
        """
        self._night_mode = night
        self.update()

    def paintEvent(self, event) -> None:
        """Paint the 7-segment display using QPainter."""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        # Background
        painter.fillRect(self.rect(), QColor("#000000"))

        # Offset to center digits horizontally
        total_width = DIGIT_SPACING * NUM_DIGITS
        offset_x = (self.width() - total_width) // 2
        offset_y = 4

        for i in range(NUM_DIGITS):
            # Determine if this digit is lit in night mode
            lit = True
            if self._night_mode and i >= 4:
                lit = False

            digit_x = offset_x + i * DIGIT_SPACING
            self._draw_digit(painter, digit_x, offset_y, i, lit)

        painter.end()

    def _draw_digit(self, painter: QPainter, ox: float, oy: float,
                    index: int, lit: bool) -> None:
        """Draw a single digit at the given offset.

        Args:
            painter: Active QPainter.
            ox, oy: Top-left offset for this digit.
            index: Digit position (0-7).
            lit: Whether this digit should be illuminated.
        """
        ch = self._text[index] if index < len(self._text) else ' '
        seg_bits = SEGMENT_MAP.get(ch, 0)

        # Draw the 7 segments
        for seg_idx, seg_points in enumerate(SEGMENT_POLYGONS):
            is_on = (seg_bits >> seg_idx) & 1
            if lit and is_on:
                color = COLOR_ON
            elif lit:
                color = COLOR_OFF
            else:
                color = COLOR_NIGHT_OFF

            # Build polygon offset by digit position
            poly = QPolygonF()
            for px, py in seg_points:
                poly.append(QPointF(ox + px, oy + py))

            painter.setPen(Qt.NoPen)
            painter.setBrush(QBrush(color))
            painter.drawPolygon(poly)

        # Draw decimal point — now from character's segment code (bit 7).
        # '.' occupies its own digit and shows only the DP circle.
        dp_on = (seg_bits >> 7) & 1
        if dp_on:
            dp_color = COLOR_ON if lit else COLOR_NIGHT_OFF
        else:
            dp_color = COLOR_NIGHT_OFF if not lit else COLOR_OFF

        dp_r = 3
        dp_cx = ox + DIGIT_W + 2
        dp_cy = oy + DIGIT_H - dp_r - 2
        painter.setPen(Qt.NoPen)
        painter.setBrush(QBrush(dp_color))
        painter.drawEllipse(QPointF(dp_cx, dp_cy), dp_r, dp_r)
