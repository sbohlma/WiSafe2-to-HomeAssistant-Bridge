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
from homeassistant.const import STATE_UNAVAILABLE, STATE_UNKNOWN
from homeassistant.core import HomeAssistant, State, callback
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.restore_state import RestoreEntity
from homeassistant.helpers.update_coordinator import CoordinatorEntity
from homeassistant.util import dt as dt_util

from .const import DOMAIN, ATTR_DEVICE_ID, ATTR_LAST_SEEN, ATTR_LAST_UPDATED
from .coordinator import WiSafe2Coordinator, WiSafe2Device


def _parse_timestamp(value: Any) -> datetime | None:
    """Parse an ISO timestamp restored from a previous state, or None."""
    if not value or value in (STATE_UNKNOWN, STATE_UNAVAILABLE):
        return None
    return dt_util.parse_datetime(str(value))


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
            WiSafe2DeviceBatteryUpdatedSensor(coordinator, config_entry, device),
            WiSafe2DeviceBaseSensor(coordinator, config_entry, device),
            WiSafe2DeviceBaseUpdatedSensor(coordinator, config_entry, device),
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


class WiSafe2DeviceSensorBase(CoordinatorEntity, RestoreEntity, SensorEntity):
    """Base class for device sensors.

    Alarms only report battery/base state when they send a message (tests,
    status reports, emergencies), which can be days or weeks apart. The device
    object starts empty on every HA restart, so each sensor restores its last
    recorded state and seeds the shared device object with it. That keeps the
    last known value visible instead of "unknown" until the next report.
    """

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

    async def async_added_to_hass(self) -> None:
        """Restore the last recorded value into the device object."""
        await super().async_added_to_hass()
        last_state = await self.async_get_last_state()
        if last_state is None or last_state.state in (STATE_UNKNOWN, STATE_UNAVAILABLE):
            return
        self._restore_from_state(last_state)
        self.async_write_ha_state()

    def _restore_from_state(self, last_state: State) -> None:
        """Seed the device from a restored state. Subclasses override.

        Only fill fields that are still empty: a live message may already
        have arrived between setup and restore, and it must win.
        """


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
    def extra_state_attributes(self) -> dict[str, Any]:
        """Return extra state attributes."""
        attrs = super().extra_state_attributes
        updated = self._device.battery_updated
        attrs[ATTR_LAST_UPDATED] = updated.isoformat() if updated else None
        return attrs

    def _restore_from_state(self, last_state: State) -> None:
        """Restore battery status and when it was reported."""
        if self._device.battery_status == "unknown":
            self._device.battery_status = last_state.state
            self._device.battery_updated = _parse_timestamp(
                last_state.attributes.get(ATTR_LAST_UPDATED)
            )

    @property
    def icon(self) -> str:
        """Return the icon based on battery status."""
        status = self._device.battery_status.lower()
        if status in ("ok", "good"):
            return "mdi:battery"
        if self._device.is_battery_low:
            return "mdi:battery-low"
        if status == "missing":
            return "mdi:battery-off"
        return "mdi:battery-unknown"


class WiSafe2DeviceBatteryUpdatedSensor(WiSafe2DeviceSensorBase):
    """Timestamp of the last battery status report from the alarm."""

    _attr_name = "Battery Last Updated"
    _attr_device_class = SensorDeviceClass.TIMESTAMP
    _attr_icon = "mdi:battery-clock"

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator, config_entry, device)
        self._attr_unique_id = f"{device.device_id}_battery_updated"

    @property
    def native_value(self) -> datetime | None:
        """Return when the battery status was last reported."""
        return self._device.battery_updated

    def _restore_from_state(self, last_state: State) -> None:
        """Restore the timestamp."""
        if self._device.battery_updated is None:
            self._device.battery_updated = _parse_timestamp(last_state.state)


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
    def extra_state_attributes(self) -> dict[str, Any]:
        """Return extra state attributes."""
        attrs = super().extra_state_attributes
        updated = self._device.base_updated
        attrs[ATTR_LAST_UPDATED] = updated.isoformat() if updated else None
        return attrs

    def _restore_from_state(self, last_state: State) -> None:
        """Restore base status and when it was reported."""
        if self._device.base_status == "unknown":
            self._device.base_status = last_state.state
            self._device.base_updated = _parse_timestamp(
                last_state.attributes.get(ATTR_LAST_UPDATED)
            )

    @property
    def icon(self) -> str:
        """Return the icon based on base status."""
        if self._device.is_on_base:
            return "mdi:home"
        if self._device.is_off_base:
            return "mdi:home-alert"
        if self._device.base_status.lower() == "missing":
            return "mdi:home-off"
        return "mdi:home-outline"


class WiSafe2DeviceBaseUpdatedSensor(WiSafe2DeviceSensorBase):
    """Timestamp of the last base status report from the alarm."""

    _attr_name = "Base Last Updated"
    _attr_device_class = SensorDeviceClass.TIMESTAMP
    _attr_icon = "mdi:home-clock"

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator, config_entry, device)
        self._attr_unique_id = f"{device.device_id}_base_updated"

    @property
    def native_value(self) -> datetime | None:
        """Return when the base status was last reported."""
        return self._device.base_updated

    def _restore_from_state(self, last_state: State) -> None:
        """Restore the timestamp."""
        if self._device.base_updated is None:
            self._device.base_updated = _parse_timestamp(last_state.state)


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

    def _restore_from_state(self, last_state: State) -> None:
        """Restore the last test result."""
        if self._device.last_test_result is None:
            self._device.last_test_result = last_state.state

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

    def _restore_from_state(self, last_state: State) -> None:
        """Restore the last seen timestamp."""
        if self._device.last_seen is None:
            self._device.last_seen = _parse_timestamp(last_state.state)
