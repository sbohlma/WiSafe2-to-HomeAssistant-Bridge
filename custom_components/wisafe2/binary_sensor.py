"""Binary sensor platform for WiSafe2 FireAngel Bridge."""
from __future__ import annotations

from typing import Any

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN, ATTR_DEVICE_ID, DEVICE_TYPE_SMOKE, DEVICE_TYPE_CO, DEVICE_TYPE_HEAT
from .coordinator import WiSafe2Coordinator, WiSafe2Device


async def async_setup_entry(
    hass: HomeAssistant,
    config_entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up WiSafe2 binary sensors from a config entry."""
    coordinator: WiSafe2Coordinator = hass.data[DOMAIN][config_entry.entry_id]

    entities: list[BinarySensorEntity] = []

    # Bridge connectivity
    entities.append(WiSafe2BridgeConnectivitySensor(coordinator, config_entry))

    # Device sensors
    for device_id, device in coordinator.devices.items():
        entities.extend([
            WiSafe2DeviceConnectivitySensor(coordinator, config_entry, device),
            WiSafe2DeviceProblemSensor(coordinator, config_entry, device),
        ])

        # Add smoke sensor for smoke/heat devices
        if device.device_type in [DEVICE_TYPE_SMOKE, DEVICE_TYPE_HEAT, None]:
            entities.append(
                WiSafe2DeviceSmokeSensor(coordinator, config_entry, device)
            )

        # Add CO sensor for CO devices
        if device.device_type in [DEVICE_TYPE_CO, None]:
            entities.append(
                WiSafe2DeviceCOSensor(coordinator, config_entry, device)
            )

    async_add_entities(entities)


class WiSafe2BridgeConnectivitySensor(CoordinatorEntity, BinarySensorEntity):
    """Bridge connectivity binary sensor."""

    _attr_has_entity_name = True
    _attr_name = "Connected"
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator)
        self._attr_unique_id = f"{config_entry.entry_id}_bridge_connectivity"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, "bridge")},
        )

    @property
    def is_on(self) -> bool:
        """Return true if bridge is connected."""
        return self.coordinator.bridge_online


class WiSafe2DeviceBinarySensorBase(CoordinatorEntity, BinarySensorEntity):
    """Base class for device binary sensors."""

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


class WiSafe2DeviceConnectivitySensor(WiSafe2DeviceBinarySensorBase):
    """Device connectivity binary sensor."""

    _attr_name = "Connected"
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator, config_entry, device)
        self._attr_unique_id = f"{device.device_id}_connectivity"

    @property
    def is_on(self) -> bool:
        """Return true if device is connected."""
        return self._device.is_online


class WiSafe2DeviceProblemSensor(WiSafe2DeviceBinarySensorBase):
    """Device problem binary sensor."""

    _attr_name = "Problem"
    _attr_device_class = BinarySensorDeviceClass.PROBLEM

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator, config_entry, device)
        self._attr_unique_id = f"{device.device_id}_problem"

    @property
    def is_on(self) -> bool:
        """Return true if device has a problem."""
        # Problem if low battery, off base, or failed test
        battery_low = self._device.battery_status.lower() == "low"
        off_base = self._device.base_status.lower() in ["removed", "off_base"]
        test_failed = (
            self._device.last_test_result
            and "fail" in self._device.last_test_result.lower()
        )
        return battery_low or off_base or test_failed

    @property
    def extra_state_attributes(self) -> dict[str, Any]:
        """Return extra state attributes."""
        attrs = super().extra_state_attributes
        problems = []
        if self._device.battery_status.lower() == "low":
            problems.append("Low Battery")
        if self._device.base_status.lower() in ["removed", "off_base"]:
            problems.append("Off Base")
        if (
            self._device.last_test_result
            and "fail" in self._device.last_test_result.lower()
        ):
            problems.append("Test Failed")
        attrs["problems"] = problems
        return attrs


class WiSafe2DeviceSmokeSensor(WiSafe2DeviceBinarySensorBase):
    """Device smoke/fire alarm binary sensor."""

    _attr_name = "Smoke"
    _attr_device_class = BinarySensorDeviceClass.SMOKE

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator, config_entry, device)
        self._attr_unique_id = f"{device.device_id}_smoke"

    @property
    def is_on(self) -> bool:
        """Return true if smoke/fire alarm is active.

        Arduino event: "FIRE EMERGENCY"
        """
        event = self._device.last_event
        if event:
            event_upper = event.upper()
            if "FIRE EMERGENCY" in event_upper:
                return True
        return False


class WiSafe2DeviceCOSensor(WiSafe2DeviceBinarySensorBase):
    """Device CO alarm binary sensor."""

    _attr_name = "Carbon Monoxide"
    _attr_device_class = BinarySensorDeviceClass.CO

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
        device: WiSafe2Device,
    ) -> None:
        """Initialize the sensor."""
        super().__init__(coordinator, config_entry, device)
        self._attr_unique_id = f"{device.device_id}_co"

    @property
    def is_on(self) -> bool:
        """Return true if CO alarm is active.

        Arduino event: "CARBON MONOXIDE EMERGENCY"
        """
        event = self._device.last_event
        if event:
            event_upper = event.upper()
            if "CARBON MONOXIDE EMERGENCY" in event_upper:
                return True
        return False
