"""Voice command thread using Vosk offline ASR."""
from __future__ import annotations

import json
import os
import re
from typing import Optional

from PyQt5.QtCore import QThread, pyqtSignal


class VoiceWorker(QThread):
    recognized = pyqtSignal(str)
    alarm_cmd = pyqtSignal(int, int)
    weather_cmd = pyqtSignal()

    _TIME_RE = re.compile(
        r"([\d零一二三四五六七八九十两]+)\s*点"
        r"\s*([\d零一二三四五六七八九十两]*)\s*(?:分)?"
    )
    _ALARM_KW = re.compile(r"闹钟|提醒|叫我")
    _WEATHER_KW = re.compile(r"天气|气温")
    _CN_NUM = {
        "零": 0,
        "一": 1,
        "二": 2,
        "两": 2,
        "三": 3,
        "四": 4,
        "五": 5,
        "六": 6,
        "七": 7,
        "八": 8,
        "九": 9,
        "十": 10,
    }

    def __init__(self, parent=None):
        super().__init__(parent)
        self._running = True

    def stop(self) -> None:
        self._running = False

    def run(self) -> None:
        try:
            import speech_recognition as sr
            import vosk
        except ImportError as exc:
            self.recognized.emit(f"缺少语音依赖: {exc}")
            return

        model_path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "vosk-model-small-cn-0.22",
        )
        if not os.path.isdir(model_path):
            self.recognized.emit(f"Vosk模型未找到: {model_path}")
            return

        try:
            vosk.SetLogLevel(-1)
            model = vosk.Model(model_path)
        except Exception as exc:
            self.recognized.emit(f"Vosk模型加载失败: {exc}")
            return

        try:
            mic = sr.Microphone()
        except Exception as exc:
            self.recognized.emit(f"麦克风不可用: {exc}")
            return

        recognizer = sr.Recognizer()
        recognizer.energy_threshold = 300
        recognizer.pause_threshold = 0.8
        self.recognized.emit("模型已加载, 正在监听...")

        while self._running:
            try:
                with mic as source:
                    audio = recognizer.listen(
                        source, timeout=1, phrase_time_limit=5
                    )
                rec = vosk.KaldiRecognizer(model, 16000)
                data = audio.get_raw_data(convert_rate=16000, convert_width=2)
                text = ""
                if rec.AcceptWaveform(data):
                    result = json.loads(rec.Result())
                    text = result.get("text", "").strip()
                else:
                    partial = json.loads(rec.PartialResult())
                    text = partial.get("partial", "").strip()
                if text:
                    self.recognized.emit(text)
                    self._dispatch(text)
            except sr.WaitTimeoutError:
                continue
            except Exception as exc:
                self.recognized.emit(f"语音识别错误: {exc}")

    def _dispatch(self, text: str) -> None:
        try:
            if self._ALARM_KW.search(text):
                match = self._TIME_RE.search(text)
                if match:
                    hour = self._parse_num(match.group(1))
                    minute_text = (match.group(2) or "").strip()
                    minute = self._parse_num(minute_text) if minute_text else 0
                    if hour is not None and minute is not None:
                        if 0 <= hour <= 23 and 0 <= minute <= 59:
                            self.alarm_cmd.emit(hour, minute)
                            return
            if self._WEATHER_KW.search(text):
                self.weather_cmd.emit()
        except Exception as exc:
            self.recognized.emit(f"语音命令分发错误: {exc}")

    def _parse_num(self, text: str) -> Optional[int]:
        text = text.strip()
        if not text:
            return 0
        try:
            return int(text)
        except ValueError:
            pass

        if text in self._CN_NUM:
            return self._CN_NUM[text]

        if "十" in text:
            left, _, right = text.partition("十")
            tens = self._CN_NUM.get(left, 1) if left else 1
            ones = self._CN_NUM.get(right, 0) if right else 0
            return tens * 10 + ones

        total = 0
        for char in text:
            if char not in self._CN_NUM:
                return None
            total = total * 10 + self._CN_NUM[char]
        return total
