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

# Device models mapping
DEVICE_MODELS = {
    "1103": {"name": "WST-630", "type": DEVICE_TYPE_COMBINED, "description": "Smoke/Strobe Unit"},
    "0301": {"name": "W2-CO-10X", "type": DEVICE_TYPE_CO, "description": "Carbon Monoxide Alarm"},
    "0401": {"name": "FP2620W2", "type": DEVICE_TYPE_SMOKE, "description": "Smoke Alarm"},
    "0501": {"name": "FP1720W2", "type": DEVICE_TYPE_HEAT, "description": "Heat Alarm"},
    "0601": {"name": "W2-SVP-630", "type": DEVICE_TYPE_STROBE, "description": "Strobe Unit"},
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
