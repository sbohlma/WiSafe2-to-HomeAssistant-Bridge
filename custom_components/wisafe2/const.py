"""Constants for the WiSafe2 FireAngel Bridge integration."""

DOMAIN = "wisafe2"

# Configuration keys
CONF_SERIAL_PORT = "serial_port"
CONF_BAUD_RATE = "baud_rate"

# Default values
DEFAULT_BAUD_RATE = 115200

# Device types
DEVICE_TYPE_SMOKE = "smoke"
DEVICE_TYPE_HEAT = "heat"
DEVICE_TYPE_CO = "co"
DEVICE_TYPE_STROBE = "strobe"
DEVICE_TYPE_COMBINED = "combined"
DEVICE_TYPE_BRIDGE = "bridge"
DEVICE_TYPE_SIREN = "siren"
DEVICE_TYPE_WATER = "water"
DEVICE_TYPE_UNKNOWN = "unknown"

# Device models mapping
DEVICE_MODELS = {
# extracted from W2-MM Mesh Monitor Software:
    "E702": {"name": "WSM-1", "type": DEVICE_TYPE_SMOKE, "description": "AC smoke alarm"},
    "1103": {"name": "WST-630", "type": DEVICE_TYPE_COMBINED, "description": "Smoke/Strobe Unit"},
    "7803": {"name": "W2-CO-10X #1", "type": DEVICE_TYPE_CO, "description": "Carbon monoxide alarm"},
    "8B03": {"name": "W2-SVP-630 #1", "type": DEVICE_TYPE_STROBE, "description": "Strobe/visual alarm"},
    "A203": {"name": "W2-LFS-630", "type": DEVICE_TYPE_SIREN, "description": "Low frequency sounder"},
    "FE03": {"name": "IFG-100", "type": DEVICE_TYPE_UNKNOWN, "description": "IFG device"},
    "1104": {"name": "WHT-630", "type": DEVICE_TYPE_HEAT, "description": "Heat alarm"},
    "1404": {"name": "WHM-1", "type": DEVICE_TYPE_HEAT, "description": "AC heat alarm"},
    "1C04": {"name": "IFG-200", "type": DEVICE_TYPE_UNKNOWN, "description": "IFG device"},
    "4504": {"name": "W2-TSL", "type": DEVICE_TYPE_UNKNOWN, "description": "TSL device"},
    "7C04": {"name": "ST-630-DE", "type": DEVICE_TYPE_SMOKE, "description": "Smoke alarm"},
    "8504": {"name": "WETA-10X", "type": DEVICE_TYPE_WATER, "description": "Water/leak alarm"},
    "C304": {"name": "W2-SVP-630 #2", "type": DEVICE_TYPE_STROBE, "description": "Strobe/visual alarm"},
# to verify:
    "0301": {"name": "W2-CO-10X #2", "type": DEVICE_TYPE_CO, "description": "Carbon Monoxide Alarm"},
    "0401": {"name": "FP2620W2", "type": DEVICE_TYPE_SMOKE, "description": "Smoke Alarm"},
    "0501": {"name": "FP1720W2", "type": DEVICE_TYPE_HEAT, "description": "Heat Alarm"},
    "0601": {"name": "W2-SVP-630 #3", "type": DEVICE_TYPE_STROBE, "description": "Strobe Unit"},
}

# Event types from radio
EVENT_TYPE_FIRE = 0x81
EVENT_TYPE_CO = 0x41

# Message types
MSG_TEST = 0x70
MSG_EMERGENCY = 0x50
MSG_STATUS = 0x71
MSG_MISSING = 0xD2
MSG_PAIRING = 0xD1

# Status flags
STATUS_ON_BASE = 0x04
STATUS_LOW_BATTERY = 0x42
STATUS_OFF_BASE = 0x00

# Commands
CMD_TEST_CO = "1~"
CMD_TEST_SMOKE = "2~"
CMD_TEST_ALL = "3~"
CMD_EMERGENCY_CO = "4~"
CMD_EMERGENCY_SMOKE = "5~"
CMD_SILENCE_CO = "6~"
CMD_SILENCE_SMOKE = "7~"
CMD_GET_PAIRING = "8~"
CMD_START_PAIRING = "9~"

# Heartbeat interval (seconds)
HEARTBEAT_INTERVAL = 25
HEARTBEAT_TIMEOUT = 35

# Platforms
PLATFORMS = ["sensor", "binary_sensor", "button"]

# Attributes
ATTR_DEVICE_ID = "device_id"
ATTR_MODEL = "model"
ATTR_LAST_SEEN = "last_seen"
ATTR_EVENT_TYPE = "event_type"
ATTR_TEST_RESULT = "test_result"
ATTR_BATTERY_STATUS = "battery_status"
ATTR_BASE_STATUS = "base_status"
