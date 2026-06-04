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
DP_RADIUS = 4

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
SEGMENT_MAP: dict = {
    '0': 0b0111111,  # a b c d e f
    '1': 0b0000110,  # b c
    '2': 0b1011011,  # a b d e g
    '3': 0b1001111,  # a b c d g
    '4': 0b1100110,  # b c f g
    '5': 0b1101101,  # a c d f g
    '6': 0b1111101,  # a c d e f g
    '7': 0b0000111,  # a b c
    '8': 0b1111111,  # a b c d e f g
    '9': 0b1101111,  # a b c d f g
    'A': 0b1110111,  # a b c e f g
    'B': 0b1111100,  # b c d e f g? no -- actually B is often shown as 8 with only b,c,d,e,f,g? let me use standard: b c d e f g... Actually standard: b c d e f g lit for lowercase b? The spec says "54.03.21" for time so we need to handle '.'  too. Let me keep . mapped to just dp, not segments.
    'C': 0b0111001,  # a d e f
    'D': 0b1011110,  # b c d e g  -- typical "d" shape
    'E': 0b1111001,  # a d e f g
    'F': 0b1110001,  # a e f g
    'H': 0b1110110,  # b c e f g
    'L': 0b0111000,  # d e f
    'O': 0b0111111,  # a b c d e f (same as 0)
    'P': 0b1110011,  # a b e f g
    'S': 0b1101101,  # a c d f g (same as 5)
    'U': 0b0111110,  # b c d e f
    '-': 0b1000000,  # g only
    '_': 0b0001000,  # d only
    ' ': 0b0000000,  # blank
    '.': 0b0000000,  # dp only, no segments
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

        # Draw decimal point
        dp_on = (self._dp_hex >> index) & 1
        if lit and dp_on:
            dp_color = COLOR_ON
        elif lit:
            dp_color = COLOR_OFF
        else:
            dp_color = COLOR_NIGHT_OFF

        dp_cx = ox + DIGIT_W - DP_RADIUS - 2
        dp_cy = oy + DIGIT_H - DP_RADIUS - 2
        painter.setPen(Qt.NoPen)
        painter.setBrush(QBrush(dp_color))
        painter.drawEllipse(QPointF(dp_cx, dp_cy), DP_RADIUS, DP_RADIUS)
