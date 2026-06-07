"""Protocol parser module for the S800 clock communication protocol.

Handles parsing incoming serial data into structured frames and formatting
outgoing commands according to the protocol specification.
"""

from __future__ import annotations

import re
from typing import Any, Dict, List, Optional


class ProtocolParser:
    """Parses and formats S800 clock protocol messages.

    The protocol uses ASCII text frames delimited by CR/LF line endings.
    Commands are asterisk-prefixed; responses are "OK", "ERROR", or event lines.
    """

    # Recognized frame type patterns (case-insensitive)
    _PATTERNS = [
        (re.compile(r"^\*EVT:KEY\s+(\S+)", re.IGNORECASE), "evt_key"),
        (re.compile(r"^\*EVT:ALARM\s+OFF", re.IGNORECASE), "evt_alarm"),
        (re.compile(r"^\*EVT:ALARM", re.IGNORECASE), "evt_alarm"),
        (re.compile(r"^\*EVT:EDIT\s+(\S+)\s+(.*)", re.IGNORECASE), "evt_edit"),
        (re.compile(r"^\*EVT:DISP\s+(.+?)\s+([0-9A-Fa-f]{1,2})$", re.IGNORECASE), "evt_disp"),
        (re.compile(r"^\*EVT:LED\s+([0-9A-Fa-f]{1,2})", re.IGNORECASE), "evt_led"),
        (re.compile(r"^\*EVT:MODE\s+(\S+)", re.IGNORECASE), "evt_mode"),
        (re.compile(r"^\*PONG\s+(\d+)", re.IGNORECASE), "pong"),
        (re.compile(r"^OK\s+(.*)", re.IGNORECASE), "ok"),
        (re.compile(r"^OK$", re.IGNORECASE), "ok"),
        (re.compile(r"^ERROR$", re.IGNORECASE), "error"),
    ]

    @staticmethod
    def parse_incoming(data: bytes) -> List[Dict[str, Any]]:
        """Parse raw bytes into a list of structured frame dicts.

        Splits input on any combination of CR and LF characters, discards
        empty lines, and attempts to classify each line.

        Args:
            data: Raw bytes received from the serial port.

        Returns:
            List of dicts, each with at least 'type' and 'raw' keys.
            Extra keys depend on the frame type.
        """
        if not data:
            return []

        # Decode as ASCII, replacing non-decodable bytes
        try:
            text = data.decode("ascii", errors="replace")
        except Exception:
            return []

        # Split on any CR/LF combination
        lines = re.split(r"[\r\n]+", text)
        frames: List[Dict[str, Any]] = []

        for line in lines:
            stripped = line.strip()
            if not stripped:
                continue
            frame = ProtocolParser.parse_response(stripped)
            frames.append(frame)

        return frames

    @staticmethod
    def parse_response(line: str) -> Dict[str, Any]:
        """Parse a single response or event line into a structured dict.

        Args:
            line: A single line of text (already stripped of whitespace).

        Returns:
            A dict with 'type' and 'raw' keys, plus type-specific fields.
            Unknown frames have type 'unknown'.
        """
        frame: Dict[str, Any] = {"type": "unknown", "raw": line}

        # Normalize whitespace: collapse multiple spaces to single
        normalized = re.sub(r"\s+", " ", line.strip())

        for pattern, ftype in ProtocolParser._PATTERNS:
            match = pattern.match(normalized)
            if match:
                frame["type"] = ftype
                groups = match.groups()

                if ftype == "evt_key":
                    frame["key_name"] = groups[0].upper()
                elif ftype == "evt_alarm":
                    # Check if OFF was matched
                    frame["enabled"] = "OFF" not in normalized.upper()
                elif ftype == "evt_edit":
                    frame["edit_type"] = groups[0].upper()
                    frame["edit_value"] = groups[1].strip()
                elif ftype == "evt_disp":
                    frame["seg_text"] = groups[0]
                    frame["dp_hex"] = int(groups[1], 16)
                elif ftype == "evt_led":
                    frame["led_byte"] = int(groups[0], 16)
                elif ftype == "evt_mode":
                    frame["mode_state"] = groups[0].upper()
                elif ftype == "pong":
                    frame["uptime_s"] = int(groups[0])
                elif ftype == "ok":
                    frame["data"] = groups[0].strip() if len(groups) > 0 and groups[0] else ""
                elif ftype == "error":
                    pass  # no extra fields

                break

        return frame

    @staticmethod
    def format_command(
        cmd: str, subcmd: Optional[str] = None, params: Optional[List[str]] = None
    ) -> str:
        """Format a command string according to the S800 protocol.

        Examples:
            format_command('RST') -> "*RST"
            format_command('SET', 'DATE', ['24', '06', '04']) -> "*SET:DATE 24 06 04"
            format_command('SET', 'ALARM', ['OFF']) -> "*SET:ALARM OFF"
            format_command('GET', 'TIME') -> "*GET:TIME"

        Args:
            cmd: Top-level command (e.g., 'SET', 'GET', 'RST', 'PING').
            subcmd: Sub-command after colon (e.g., 'DATE', 'TIME').
            params: List of parameter strings to append with spaces.

        Returns:
            Formatted command string with leading asterisk.
        """
        if subcmd:
            base = f"*{cmd.upper()}:{subcmd.upper()}"
        else:
            base = f"*{cmd.upper()}"

        if params:
            return f"{base} {' '.join(str(p) for p in params)}"
        return base

    @staticmethod
    def validate_abbreviation(user_input: str, full_form: str) -> bool:
        """Check if user_input is a valid abbreviation of full_form.

        The rule: uppercase characters in full_form are required;
        lowercase characters are optional. The user_input must contain
        the uppercase chars in order and may optionally include any
        lowercase chars in order.

        Args:
            user_input: The abbreviated input string.
            full_form: The full command form (mixed case).

        Returns:
            True if user_input matches the abbreviation rules for full_form.
        """
        # Build a regex from full_form: uppercase chars are required,
        # lowercase chars are optional.
        pattern_parts = []
        for ch in full_form:
            if ch.isupper():
                pattern_parts.append(re.escape(ch.lower()))
                pattern_parts.append(re.escape(ch.upper()))
                pattern_parts[-2] = f"[{pattern_parts[-2]}"
                pattern_parts[-1] = f"{pattern_parts[-1]}]"
            else:
                # lowercase char is optional
                pattern_parts.append(f"(?:{re.escape(ch.lower())}|{re.escape(ch.upper())})?")

        pattern = "^" + "".join(pattern_parts) + "$"
        return bool(re.match(pattern, user_input))

    @staticmethod
    def is_heartbeat_frame(frame: Dict[str, Any]) -> bool:
        """Return True if the frame is a heartbeat event (DISP or LED)."""
        return frame.get("type") in ("evt_disp", "evt_led")
