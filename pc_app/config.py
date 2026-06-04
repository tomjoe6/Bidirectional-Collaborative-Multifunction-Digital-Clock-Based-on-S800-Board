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

# =========================================================================
# E1: NTP Time Synchronization defaults
# =========================================================================
NTP_DEFAULT_SERVER = "ntp.aliyun.com"
NTP_DEFAULT_TIMEOUT = 3  # seconds

# NTP LED control
NTP_LED_D6_MASK = 0x40     # D6 bitmask for *SET:LED
NTP_LED_DURATION_MS = 5000  # how long D6 stays lit after successful sync
NTP_STATUS_DISPLAY_MS = 2000  # how long "NTP ✓" stays in status bar

# =========================================================================
# E2: Weather API defaults
# =========================================================================
WEATHER_DEFAULT_LOCATION = "Shanghai,CN"
WEATHER_DEFAULT_CACHE_TTL = 1800  # 30 minutes
