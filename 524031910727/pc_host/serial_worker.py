"""QThread-based serial communication worker.

Handles serial port I/O in a background thread to keep the GUI responsive.
"""

from __future__ import annotations

from typing import Optional

import serial
import serial.tools.list_ports
from PyQt5.QtCore import QMutex, QThread, pyqtSignal


class SerialWorker(QThread):
    """Background thread for serial port communication.

    Opens a serial port connection, reads incoming data in a loop,
    and emits signals for received data, connection status, and errors.
    """

    # Signals
    connected = pyqtSignal(bool, str)  # True + port name, or False + error message
    received = pyqtSignal(bytes)       # Raw bytes received from serial port
    error = pyqtSignal(str)            # Error message string

    def __init__(self, port: str, baud: int = 115200, timeout: float = 1.0) -> None:
        """Initialize the serial worker.

        Args:
            port: Serial port name (e.g., 'COM3' on Windows, '/dev/ttyUSB0' on Linux).
            baud: Baud rate (default 115200).
            timeout: Read timeout in seconds.
        """
        super().__init__()
        self._port_name: str = port
        self._baud: int = baud
        self._timeout: float = timeout
        self._serial: Optional[serial.Serial] = None
        self._running: bool = False
        self._mutex: QMutex = QMutex()
        self._send_buffer: bytearray = bytearray()

    def run(self) -> None:
        """Main thread loop.

        Opens the serial port, enters a read loop, and emits data as received.
        Exits cleanly when stop() is called or on error.
        """
        self._running = True

        try:
            self._serial = serial.Serial(
                port=self._port_name,
                baudrate=self._baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self._timeout,
                write_timeout=self._timeout,
            )
        except Exception as e:
            self._running = False
            self.connected.emit(False, f"串口打开失败: {e}")
            self.error.emit(f"串口 {self._port_name} 打开失败: {e}")
            return

        self.connected.emit(True, self._port_name)

        # Clear any stale data
        if self._serial and self._serial.is_open:
            try:
                self._serial.reset_input_buffer()
                self._serial.reset_output_buffer()
            except Exception:
                pass

        while self._running and self._serial and self._serial.is_open:
            try:
                # Flush send buffer
                self._flush_send_buffer()

                # Read available data
                if self._serial.in_waiting > 0:
                    data = self._serial.read(self._serial.in_waiting)
                    if data:
                        self.received.emit(data)
                else:
                    # Small sleep to prevent CPU spinning
                    self.msleep(10)

            except serial.SerialException as e:
                if self._running:
                    self.error.emit(f"串口读写错误: {e}")
                    self._running = False
                    self.connected.emit(False, f"连接丢失: {e}")
                break
            except Exception as e:
                if self._running:
                    self.error.emit(f"未知错误: {e}")
                break

        # Cleanup
        self._close_serial()

    def send(self, data: bytes) -> None:
        """Queue data for transmission over the serial port.

        Thread-safe: data is appended to a mutex-protected buffer and
        will be written during the next read loop iteration.

        Args:
            data: Raw bytes to send.
        """
        self._mutex.lock()
        try:
            self._send_buffer.extend(data)
        finally:
            self._mutex.unlock()

    def _flush_send_buffer(self) -> None:
        """Write any queued data to the serial port (called from run loop)."""
        self._mutex.lock()
        try:
            if self._send_buffer and self._serial and self._serial.is_open:
                try:
                    self._serial.write(bytes(self._send_buffer))
                    self._serial.flush()
                except Exception:
                    pass  # will be caught in main loop
                self._send_buffer.clear()
        finally:
            self._mutex.unlock()

    def stop(self) -> None:
        """Signal the worker to stop and clean up.

        Sets the running flag to False so the read loop exits.
        """
        self._running = False

    def is_connected(self) -> bool:
        """Check if the serial port is currently open.

        Returns:
            True if the serial port is open and the worker is running.
        """
        return (
            self._running
            and self._serial is not None
            and self._serial.is_open
        )

    def _close_serial(self) -> None:
        """Close the serial port if open."""
        if self._serial and self._serial.is_open:
            try:
                self._serial.close()
            except Exception:
                pass
        self._serial = None

    @staticmethod
    def list_ports() -> list:
        """List available serial ports.

        Returns:
            List of port device names (strings).
        """
        ports = serial.tools.list_ports.comports()
        return [p.device for p in sorted(ports)]

    @staticmethod
    def list_ports_with_description() -> list:
        """List available serial ports with descriptions.

        Returns:
            List of (device, description) tuples.
        """
        ports = serial.tools.list_ports.comports()
        return [(p.device, p.description) for p in sorted(ports)]
