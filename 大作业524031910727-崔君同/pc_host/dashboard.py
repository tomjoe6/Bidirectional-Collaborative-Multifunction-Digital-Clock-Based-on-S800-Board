"""Data dashboard with SQLite storage and real-time matplotlib charts."""
from __future__ import annotations
import sqlite3, time, os, threading
from typing import Optional

from PyQt5.QtCore import QTimer, Qt
from PyQt5.QtWidgets import QHBoxLayout, QLabel, QVBoxLayout, QWidget
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure
import matplotlib
matplotlib.use("Qt5Agg")
matplotlib.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "DejaVu Sans"]
matplotlib.rcParams["axes.unicode_minus"] = False

DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "s800_log.db")
KEEP_DAYS = 7


def _skip_heartbeat(direction: str, content: str) -> bool:
    """Ignore EVT:DISP and EVT:LED heartbeats (every 1s, too noisy)."""
    return direction == "EVT" and ("*EVT:DISP" in content or "*EVT:LED" in content)


def classify(direction: str, content: str) -> str:
    d = direction.upper()
    c = content.upper()
    if "ALARM" in c:
        return "闹钟"
    if c.startswith("*EVT:KEY") or "KEY" in c:
        return "按键"
    if d == "ERR":
        return "错误"
    if "NTP" in c:
        return "对时"
    if "WEATHER" in c or ("MSG" in c and "天气" in c):
        return "天气"
    if d == "TX":
        return "发送"
    return None  # don't count in stats


class LogStore:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        self._conn.execute("DROP TABLE IF EXISTS events")
        self._conn.execute(
            "CREATE TABLE events ("
            "  ts REAL, category TEXT, content TEXT"
            ")"
        )
        self._conn.execute(
            "CREATE TABLE IF NOT EXISTS latency ("
            "  ts REAL, ms INTEGER"
            ")"
        )
        self._conn.commit()
        self._purge()

    def add(self, category: str, content: str) -> None:
        with self._lock:
            self._conn.execute(
                "INSERT INTO events VALUES (?,?,?)",
                (time.time(), category, content),
            )
            self._conn.commit()

    def add_latency(self, latency_ms: int) -> None:
        with self._lock:
            self._conn.execute(
                "INSERT INTO latency VALUES (?,?)",
                (time.time(), latency_ms),
            )
            self._conn.commit()

    def category_counts(self, seconds: float) -> dict:
        cutoff = time.time() - seconds
        with self._lock:
            rows = self._conn.execute(
                "SELECT category, COUNT(*) FROM events WHERE ts > ? GROUP BY category",
                (cutoff,),
            ).fetchall()
        return {c: n for c, n in rows}

    def latency_history(self, seconds: float) -> list:
        cutoff = time.time() - seconds
        with self._lock:
            rows = self._conn.execute(
                "SELECT ts, ms FROM latency WHERE ts > ? ORDER BY ts",
                (cutoff,),
            ).fetchall()
        return rows

    def _purge(self) -> None:
        cutoff = time.time() - KEEP_DAYS * 86400
        self._conn.execute("DELETE FROM events WHERE ts < ?", (cutoff,))
        self._conn.execute("DELETE FROM latency WHERE ts < ?", (cutoff,))
        self._conn.commit()


class Dashboard(QWidget):
    def __init__(self, store: Optional[LogStore] = None, parent=None) -> None:
        super().__init__(parent)
        self._store = store or LogStore()
        self._setup_ui()
        self._timer = QTimer(self)
        self._timer.setInterval(3000)
        self._timer.timeout.connect(self._refresh)
        self._timer.start()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        stats = QHBoxLayout()
        self._lbl_total = QLabel("总事件: 0")
        self._lbl_total.setStyleSheet("color:#CCC;font-size:16px;")
        stats.addWidget(self._lbl_total)
        self._lbl_latency = QLabel("延迟: --ms")
        self._lbl_latency.setStyleSheet("color:#0AF;font-size:16px;")
        stats.addWidget(self._lbl_latency)
        stats.addStretch()
        layout.addLayout(stats)

        charts = QHBoxLayout()
        self._fig = Figure(figsize=(7, 3.5), dpi=100)
        self._fig.set_facecolor("#1E1E1E")
        self._canvas = FigureCanvas(self._fig)
        charts.addWidget(self._canvas, stretch=2)

        self._tbl = QLabel("")
        self._tbl.setStyleSheet(
            "color:#CCC;font-size:23px;font-family:Consolas,monospace;"
            "background:#2A2A2A;padding:8px;border-radius:4px;"
        )
        self._tbl.setAlignment(Qt.AlignTop)
        charts.addWidget(self._tbl, stretch=1)
        layout.addLayout(charts)

    def _refresh(self) -> None:
        cats = self._store.category_counts(3600)
        total = sum(cats.values())

        # Latency chart
        lat = self._store.latency_history(600)  # last 10 min
        self._fig.clear()
        ax = self._fig.add_subplot(111)
        ax.set_facecolor("#2A2A2A")
        ax.tick_params(colors="#AAA", labelsize=8)
        ax.set_title("PING延迟 (ms)", color="#CCC", fontsize=10)
        if lat:
            times, vals = zip(*lat)
            ax.fill_between(range(len(vals)), vals, alpha=0.3, color="#0AF")
            ax.plot(vals, color="#0AF", linewidth=1)
            self._lbl_latency.setText(f"延迟: {vals[-1]}ms")
        ax.set_ylim(bottom=0)
        self._fig.tight_layout()
        self._canvas.draw()

        # Data table: special event counts
        order = [("闹钟", "#F80"), ("对时", "#0CF"),
                 ("天气", "#0F8"), ("错误", "#C00")]
        lines = ["<b>近1小时行为统计</b>", ""]
        for name, color in order:
            count = cats.get(name, 0)
            lines.append(
                f'<span style="color:{color};">■ {name:4s}</span>'
                f'  {count:5d} 次'
            )
        self._tbl.setText("<br>".join(lines))

        self._lbl_total.setText(f"总事件: {total}")
