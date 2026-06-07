"""S800 智能联网时钟系统 — PC 上位机启动入口

直接运行: python run.py
"""

import sys
import os

# Ensure the project root is on sys.path so pc_app can be imported
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pc_app.main import main

if __name__ == "__main__":
    sys.exit(main())
