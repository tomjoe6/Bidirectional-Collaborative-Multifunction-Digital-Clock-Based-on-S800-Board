"""Heartbeat timeout monitor.

Monitors the connection health by checking for periodic heartbeat
events (EVT:DISP or EVT:LED) from the S800 board.
"""

from __future__ import annotations

import time

from PyQt5.QtCore import QObject, QTimer, pyqtSignal

from config import HEARTBEAT_TIMEOUT


class HeartbeatMonitor(QObject):
    """Monitors connection health via periodic heartbeat checks.

    Uses a QTimer to periodically check if a heartbeat event was
    received within the HEARTBEAT_TIMEOUT window. If no heartbeat
    is received in time, the connection is considered lost.
    """

    timeout = pyqtSignal()            # Emitted when heartbeat is lost
    latency_updated = pyqtSignal(int) # Emitted with latency in ms

    def __init__(self, parent: QObject = None) -> None:
        """Initialize the heartbeat monitor.

        Args:
            parent: Optional parent QObject.
        """
        super().__init__(parent)
        self._last_feed_time: float = 0.0
        self._alive: bool = False
        self._timer: QTimer = QTimer(self)
        self._timer.setInterval(500)  # Check every 500ms
        self._timer.timeout.connect(self._check)

        # Track round-trip latency
        self._ping_sent_time: float = 0.0
        self._latency_ms: int = -1

    def start(self) -> None:
        """Begin heartbeat monitoring.

        Initializes the last feed time to now and starts the timer.
        """
        self._last_feed_time = time.monotonic()
        self._alive = True
        self._timer.start()

    def stop(self) -> None:
        """Stop heartbeat monitoring."""
        self._timer.stop()
        self._alive = False

    def feed(self) -> None:
        """Feed the heartbeat watchdog.

        Call this whenever an EVT:DISP or EVT:LED frame is received
        to reset the timeout counter.
        """
        now = time.monotonic()
        self._last_feed_time = now
        if not self._alive:
            self._alive = True

    def is_alive(self) -> bool:
        """Check if the connection is currently considered alive.

        Returns:
            True if heartbeats have been received within the timeout.
        """
        return self._alive

    def latency_ms(self) -> int:
        """Return the last measured round-trip latency in ms, or -1."""
        return self._latency_ms

    def record_ping_sent(self) -> None:
        """Record the time a PING command was sent (for latency measurement)."""
        self._ping_sent_time = time.monotonic()

    def record_pong_received(self) -> None:
        """Record round-trip latency upon receiving a PONG."""
        if self._ping_sent_time > 0:
            elapsed = (time.monotonic() - self._ping_sent_time) * 1000.0
            self._latency_ms = int(elapsed)
            self.latency_updated.emit(self._latency_ms)
            self._ping_sent_time = 0.0

    def _check(self) -> None:
        """Timer callback: check if heartbeat is still within timeout."""
        if not self._alive:
            return

        elapsed = time.monotonic() - self._last_feed_time
        if elapsed > HEARTBEAT_TIMEOUT:
            self._alive = False
            self.timeout.emit()
