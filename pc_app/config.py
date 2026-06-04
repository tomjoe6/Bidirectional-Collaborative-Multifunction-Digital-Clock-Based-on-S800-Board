"""Application configuration constants."""

# Serial communication
DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT = 1.0

# Heartbeat monitoring
HEARTBEAT_TIMEOUT = 3.0  # seconds without heartbeat -> connection lost

# Protocol limits
MAX_FRAME_LEN = 64

# Application metadata
APP_NAME = "S800 智能联网时钟系统"
APP_VERSION = "1.0.0"

# Log display colors
COLOR_TX = "#0066CC"    # blue - transmitted commands
COLOR_RX = "#009933"    # green - received responses
COLOR_EVT = "#996600"   # orange - events
COLOR_ERR = "#CC0000"   # red - errors
COLOR_OK = "#009933"    # green - OK responses

# Log panel
LOG_MAX_LINES = 1000

# Serial port refresh
PORT_REFRESH_INTERVAL_MS = 2000
