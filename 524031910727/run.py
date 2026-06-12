"""S800 智能联网时钟系统 — PC 上位机启动入口

直接运行: python run.py
"""

import os
import sys

ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
PC_HOST_DIR = os.path.join(ROOT_DIR, "pc_host")

os.chdir(ROOT_DIR)
sys.path = [PC_HOST_DIR, ROOT_DIR] + [
    path for path in sys.path
    if path not in ("", PC_HOST_DIR, ROOT_DIR)
]

from main import main

if __name__ == "__main__":
    sys.exit(main())
