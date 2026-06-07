"""NTP time synchronization client for the S800 clock system (Extension E1).

Fetches precise network time from an NTP server, converts UTC to local time,
and returns structured date/time values ready for *SET:DATE and *SET:TIME commands.
"""

from __future__ import annotations

import os
import socket
import time as _time
from dataclasses import dataclass
from datetime import datetime, timezone

import ntplib

from .config import NTP_DEFAULT_SERVER, NTP_DEFAULT_TIMEOUT


@dataclass
class NTPResult:
    """Result of an NTP time synchronization request.

    Attributes:
        success: True if the time was successfully fetched.
        year: 2-digit year (0-99), valid only if success.
        month: Month (1-12), valid only if success.
        day: Day (1-31), valid only if success.
        hour: Hour (0-23) in local time.
        minute: Minute (0-59).
        second: Second (0-59).
        error_msg: Human-readable error description, valid only if not success.
    """

    success: bool
    year: int = 0
    month: int = 0
    day: int = 0
    hour: int = 0
    minute: int = 0
    second: int = 0
    error_msg: str = ""


class NTPClient:
    """Client for fetching network time from an NTP server.

    Wraps the ntplib library with timeout handling and UTC-to-local
    timezone conversion. Server address is read from the NTP_SERVER
    environment variable, falling back to a default.
    """

    def __init__(self) -> None:
        """Initialize the NTP client with server from environment or default."""
        self._server: str = os.getenv("NTP_SERVER", NTP_DEFAULT_SERVER)
        timeout_str = os.getenv("NTP_TIMEOUT", "")
        self._timeout: float = NTP_DEFAULT_TIMEOUT
        if timeout_str:
            try:
                self._timeout = float(timeout_str)
            except ValueError:
                pass
        self._client = ntplib.NTPClient()

    @property
    def server(self) -> str:
        """Return the configured NTP server address."""
        return self._server

    def fetch_time(self) -> NTPResult:
        """Fetch current time from the NTP server.

        Returns:
            NTPResult with success=True and local time fields on success,
            or success=False with an error message on failure.
        """
        try:
            response = self._client.request(self._server, timeout=int(self._timeout))
        except ntplib.NTPException as exc:
            return NTPResult(
                success=False,
                error_msg=f"NTP协议错误: {exc}",
            )
        except socket.timeout:
            return NTPResult(
                success=False,
                error_msg=f"NTP请求超时 ({self._timeout}s)",
            )
        except socket.gaierror as exc:
            return NTPResult(
                success=False,
                error_msg=f"无法解析NTP服务器地址 {self._server}: {exc}",
            )
        except OSError as exc:
            return NTPResult(
                success=False,
                error_msg=f"网络错误: {exc}",
            )

        # ntplib >= 0.4.0 returns tx_time as a Unix timestamp (seconds
        # since 1970-01-01), NOT as raw NTP time (seconds since 1900).
        # Convert directly — NO epoch delta subtraction needed.
        try:
            unix_ts = int(response.tx_time)
            tm = _time.gmtime(unix_ts)
            dt_utc = datetime(
                tm.tm_year, tm.tm_mon, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec,
                tzinfo=timezone.utc,
            )
            dt_local = dt_utc.astimezone()
        except (ValueError, OSError, OverflowError) as exc:
            return NTPResult(
                success=False,
                error_msg=f"时间转换失败: {exc}",
            )

        return NTPResult(
            success=True,
            year=dt_local.year % 100,
            month=dt_local.month,
            day=dt_local.day,
            hour=dt_local.hour,
            minute=dt_local.minute,
            second=dt_local.second,
        )

    def get_date_command(self, result: NTPResult) -> str:
        """Format a *SET:DATE command from an NTP result.

        Args:
            result: A successful NTPResult.

        Returns:
            Protocol command string with line ending, e.g.
            "*SET:DATE YEAR 24 MONTH 06 DATE 04\\r\\n"
        """
        return (
            f"*SET:DATE YEAR {result.year:02d} "
            f"MONTH {result.month:02d} "
            f"DATE {result.day:02d}\r\n"
        )

    def get_time_command(self, result: NTPResult) -> str:
        """Format a *SET:TIME command from an NTP result.

        Args:
            result: A successful NTPResult.

        Returns:
            Protocol command string with line ending, e.g.
            "*SET:TIME HOUR 14 MIN 30 SEC 00\\r\\n"
        """
        return (
            f"*SET:TIME HOUR {result.hour:02d} "
            f"MIN {result.minute:02d} "
            f"SEC {result.second:02d}\r\n"
        )
