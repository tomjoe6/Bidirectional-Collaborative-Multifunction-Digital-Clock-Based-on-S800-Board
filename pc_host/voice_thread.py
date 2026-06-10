"""Voice command thread — Vosk offline ASR. Two commands: alarm + weather."""
from __future__ import annotations
import json, os, re
from typing import Optional

from PyQt5.QtCore import QThread, pyqtSignal


class VoiceWorker(QThread):
    recognized = pyqtSignal(str)
    alarm_cmd = pyqtSignal(int, int)
    weather_cmd = pyqtSignal()

    # Match time pattern: "X点" or "X点Y分" or "X点Y" anywhere in text
    # Time: "X点" or "X点Y分" — only match digits/Chinese-num after 点
    _TIME_RE = re.compile(r'([\d零一二三四五六七八九十廿卅两]+)点\s*([\d零一二三四五六七八九十廿卅两]*)\s*(?:分)?')
    _ALARM_KW = re.compile(r'闹钟')
    _WEATHER_KW = re.compile(r'天气')
    _CN_NUM = {"零":0,"一":1,"二":2,"三":3,"四":4,"五":5,"六":6,"七":7,"八":8,"九":9,
               "十":10,"两":2,"廿":20,"卅":30}

    def __init__(self, parent=None):
        super().__init__(parent)
        self._running = True

    def stop(self):
        self._running = False

    def run(self):
        try:
            import vosk
            import speech_recognition as sr
        except ImportError as e:
            self.recognized.emit(f"语音: 缺少依赖 {e}")
            return

        model_path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            "vosk-model-small-cn-0.22"
        )
        if not os.path.isdir(model_path):
            self.recognized.emit(
                f"语音: 模型未找到 {model_path}\n"
                "下载 https://alphacephei.com/vosk/models/vosk-model-small-cn-0.22.zip"
            )
            return

        self.recognized.emit("语音: 模型已加载, 正在监听...")
        vosk.SetLogLevel(-1)  # suppress C library stdout
        model = vosk.Model(model_path)
        mic = sr.Microphone()
        r = sr.Recognizer()
        r.energy_threshold = 300
        r.pause_threshold = 0.8

        while self._running:
            rec = vosk.KaldiRecognizer(model, 16000)
            try:
                with mic as source:
                    audio = r.listen(source, timeout=10, phrase_time_limit=5)
                data = audio.get_raw_data(convert_rate=16000, convert_width=2)
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
            except Exception as e:
                self.recognized.emit(f"语音错误: {e}")
                continue

    def _dispatch(self, text: str) -> None:
        try:
            if self._ALARM_KW.search(text):
                m = self._TIME_RE.search(text)
                if m:
                    h = self._parse_num(m.group(1))
                    g2 = (m.group(2) or "").strip()
                    m_val = self._parse_num(g2) if g2 else 0
                    if h is not None and m_val is not None:
                        if 0 <= h <= 23 and 0 <= m_val <= 59:
                            self.alarm_cmd.emit(h, m_val)
                            return
            if self._WEATHER_KW.search(text):
                self.weather_cmd.emit()
        except Exception as e:
            self.recognized.emit(f"dispatch错误: {e}")

    def _parse_num(self, s: str) -> Optional[int]:
        s = s.strip()
        if not s:
            return 0
        try:
            return int(s)
        except ValueError:
            pass
        if s in self._CN_NUM:
            return self._CN_NUM[s]
        total = 0
        for ch in s:
            if ch in self._CN_NUM:
                v = self._CN_NUM[ch]
                if v >= 10 and total > 0:
                    total *= v
                else:
                    total += v
        return total if total > 0 else None
