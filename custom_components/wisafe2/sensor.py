"""Sensor platform for WiSafe2 FireAngel Bridge."""
from __future__ import annotations

from datetime import datetime
from typing import Any

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorStateClass,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN, ATTR_DEVICE_ID, ATTR_LAST_SEEN
from .coordinator import WiSafe2Coordinator, WiSafe2Device


async def async_setup_entry(
    hass: HomeAssistant,
    config_entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up WiSafe2 sensors from a config entry."""
    coordinator: WiSafe2Coordinator = hass.data[DOMAIN][config_entry.entry_id]

    entities: list[SensorEntity] = []

    # Bridge sensors
    entities.extend([
        WiSafe2BridgeStatusSensor(coordinator, config_entry),
        WiSafe2BridgeMessageSensor(coordinator, config_entry),
        WiSafe2BridgeRawDataSensor(coordinator, config_entry),
    ])

    # Device sensors
    for device_id, device in coordinator.devices.items():
        entities.extend([
            WiSafe2DeviceBatterySensor(coordinator, config_entry, device),
            WiSafe2DeviceBaseSensor(coordinator, config_entry, device),
            WiSafe2DeviceEventSensor(coordinator, config_entry, device),
            WiSafe2DeviceTestResultSensor(coordinator, config_entry, device),
            WiSafe2DeviceLastSeenSensor(coordinator, config_entry, device),
        ])

    async_add_entities(entities)


class WiSafe2BridgeStatusSensor(CoordinatorEntity, SensorEntity):
    """Bridge status sensor."""

    _attr_has_entity_name = True
    _attr_name = "Status"
    _attr_icon = "mdi:bridge"

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator)
        self._attr_unique_id = f"{config_entry.entry_id}_bridge_status"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, "bridge")},
        )

    @property
    def native_value(self) -> str:
        """Return the status."""
        return "Online" if self.coordinator.bridge_online else "Offline"

    @property
    def icon(self) -> str:
        """Return the icon."""
        if self.coordinator.bridge_online:
            return "mdi:bridge"
        return "mdi:bridge-off"


class WiSafe2BridgeMessageSensor(CoordinatorEntity, SensorEntity):
    """Bridge last message sensor."""

    _attr_has_entity_name = True
    _attr_name = "Last Message"
    _attr_icon = "mdi:message-text"

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator)
        self._attr_unique_id = f"{config_entry.entry_id}_bridge_message"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, "bridge")},
        )

    @property
    def native_value(self) -> str | None:
        """Return the last message."""
        return self.coordinator.last_message


class WiSafe2BridgeRawDataSensor(CoordinatorEntity, SensorEntity):
    """Bridge raw data sensor."""

    _attr_has_entity_name = True
    _attr_name = "Raw Data"
    _attr_icon = "mdi:code-json"
    _attr_entity_registry_enabled_default = False

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator)
        self._attr_unique_id = f"{config_entry.entry_id}_bridge_raw"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, "bridge")},
        )

    @property
    def native_value(self) -> str | None:
        """Return the raw data."""
        return self.coordinator.raw_data


class WiSafe2DeviceSensorBase(CoordinatorEntity, SensorEntity):
    """Base class for device sensors."""

    _attr_has_entity_name = True

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator)
        self._device = device
        self._config_entry = config_entry
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, device.device_id)},
            manufacturer="FireAngel",
            model=device.name or "Unknown Model",
            name=device.name or f"WiSafe2 Alarm {device.device_id}",
            via_device=(DOMAIN, "bridge"),
        )

    @property
    def extra_state_attributes(self) -> dict[str, Any]:
        """Return extra state attributes."""
        return {
            ATTR_DEVICE_ID: self._device.device_id,
        }


class WiSafe2DeviceBatterySensor(WiSafe2DeviceSensorBase):
    """Device battery status sensor."""

    _attr_name = "Battery"
    _attr_icon = "mdi:battery"

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator, config_entry, device)
        self._attr_unique_id = f"{device.device_id}_battery"

    @property
    def native_value(self) -> str:
        """Return the battery status."""
        return self._device.battery_status

    @property
    def icon(self) -> str:
        """Return the icon based on battery status."""
        status = self._device.battery_status.lower()
        if status == "ok" or status == "good":
            return "mdi:battery"
        elif status == "low":
            return "mdi:battery-low"
        return "mdi:battery-unknown"


class WiSafe2DeviceBaseSensor(WiSafe2DeviceSensorBase):
    """Device base status sensor."""

    _attr_name = "Base Status"
    _attr_icon = "mdi:home"

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator, config_entry, device)
        self._attr_unique_id = f"{device.device_id}_base"

    @property
    def native_value(self) -> str:
        """Return the base status."""
        return self._device.base_status

    @property
    def icon(self) -> str:
        """Return the icon based on base status."""
        status = self._device.base_status.lower()
        if status == "attached" or status == "on_base":
            return "mdi:home"
        elif status == "removed" or status == "off_base":
            return "mdi:home-alert"
        return "mdi:home-outline"


class WiSafe2DeviceEventSensor(WiSafe2DeviceSensorBase):
    """Device last event sensor."""

    _attr_name = "Last Event"
    _attr_icon = "mdi:bell-alert"

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator, config_entry, device)
        self._attr_unique_id = f"{device.device_id}_event"

    @property
    def native_value(self) -> str | None:
        """Return the last event."""
        return self._device.last_event

    @property
    def icon(self) -> str:
        """Return the icon based on event type."""
        event = self._device.last_event
        if event and "EMERGENCY" in event:
            return "mdi:bell-alert"
        return "mdi:bell-outline"


class WiSafe2DeviceTestResultSensor(WiSafe2DeviceSensorBase):
    """Device test result sensor."""

    _attr_name = "Test Result"
    _attr_icon = "mdi:clipboard-check"

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator, config_entry, device)
        self._attr_unique_id = f"{device.device_id}_test"

    @property
    def native_value(self) -> str | None:
        """Return the test result."""
        return self._device.last_test_result

    @property
    def icon(self) -> str:
        """Return the icon based on test result."""
        result = self._device.last_test_result
        if result and "pass" in result.lower():
            return "mdi:clipboard-check"
        elif result and "fail" in result.lower():
            return "mdi:clipboard-alert"
        return "mdi:clipboard-outline"


class WiSafe2DeviceLastSeenSensor(WiSafe2DeviceSensorBase):
    """Device last seen sensor."""

    _attr_name = "Last Seen"
    _attr_device_class = SensorDeviceClass.TIMESTAMP
    _attr_icon = "mdi:clock-outline"

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator, config_entry, device)
        self._attr_unique_id = f"{device.device_id}_last_seen"

    @property
    def native_value(self) -> datetime | None:
        """Return the last seen timestamp."""
        return self._device.last_seen
