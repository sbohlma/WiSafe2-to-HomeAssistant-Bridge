"""Button platform for WiSafe2 FireAngel Bridge."""
from __future__ import annotations

from typing import Any

from homeassistant.components.button import ButtonEntity, ButtonDeviceClass
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity import DeviceInfo, EntityCategory
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import WiSafe2Coordinator


async def async_setup_entry(
    hass: HomeAssistant,
    config_entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up WiSafe2 buttons from a config entry."""
    coordinator: WiSafe2Coordinator = hass.data[DOMAIN][config_entry.entry_id]

    entities: list[ButtonEntity] = [
        WiSafe2TestCOButton(coordinator, config_entry),
        WiSafe2TestSmokeButton(coordinator, config_entry),
        WiSafe2TestAllButton(coordinator, config_entry),
        WiSafe2SilenceCOButton(coordinator, config_entry),
        WiSafe2SilenceSmokeButton(coordinator, config_entry),
        WiSafe2GetPairingButton(coordinator, config_entry),
        WiSafe2StartPairingButton(coordinator, config_entry),
    ]

    async_add_entities(entities)


class WiSafe2ButtonBase(CoordinatorEntity, ButtonEntity):
    """Base class for WiSafe2 buttons."""

    _attr_has_entity_name = True

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the button."""
        super().__init__(coordinator)
        self._config_entry = config_entry
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, "bridge")},
        )


class WiSafe2TestCOButton(WiSafe2ButtonBase):
    """Test CO button."""

    _attr_name = "Test CO Alarms"
    _attr_icon = "mdi:molecule-co"
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the button."""
        super().__init__(coordinator, config_entry)
        self._attr_unique_id = f"{config_entry.entry_id}_test_co"

    async def async_press(self) -> None:
        """Handle the button press."""
        await self.coordinator.async_test_co()


class WiSafe2TestSmokeButton(WiSafe2ButtonBase):
    """Test Smoke button."""

    _attr_name = "Test Smoke Alarms"
    _attr_icon = "mdi:fire"
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the button."""
        super().__init__(coordinator, config_entry)
        self._attr_unique_id = f"{config_entry.entry_id}_test_smoke"

    async def async_press(self) -> None:
        """Handle the button press."""
        await self.coordinator.async_test_smoke()


class WiSafe2TestAllButton(WiSafe2ButtonBase):
    """Test All Alarms button."""

    _attr_name = "Test All Alarms"
    _attr_icon = "mdi:shield-check"
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the button."""
        super().__init__(coordinator, config_entry)
        self._attr_unique_id = f"{config_entry.entry_id}_test_all"

    async def async_press(self) -> None:
        """Handle the button press."""
        await self.coordinator.async_test_all()


class WiSafe2SilenceCOButton(WiSafe2ButtonBase):
    """Silence CO Alarms button."""

    _attr_name = "Silence CO Alarms"
    _attr_icon = "mdi:volume-off"

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the button."""
        super().__init__(coordinator, config_entry)
        self._attr_unique_id = f"{config_entry.entry_id}_silence_co"

    async def async_press(self) -> None:
        """Handle the button press."""
        await self.coordinator.async_silence_co()


class WiSafe2SilenceSmokeButton(WiSafe2ButtonBase):
    """Silence Smoke Alarms button."""

    _attr_name = "Silence Smoke Alarms"
    _attr_icon = "mdi:volume-off"

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the button."""
        super().__init__(coordinator, config_entry)
        self._attr_unique_id = f"{config_entry.entry_id}_silence_smoke"

    async def async_press(self) -> None:
        """Handle the button press."""
        await self.coordinator.async_silence_smoke()


class WiSafe2GetPairingButton(WiSafe2ButtonBase):
    """Get Pairing Status button."""

    _attr_name = "Check Pairing Status"
    _attr_icon = "mdi:link-variant"
    _attr_entity_category = EntityCategory.CONFIG

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the button."""
        super().__init__(coordinator, config_entry)
        self._attr_unique_id = f"{config_entry.entry_id}_get_pairing"

    async def async_press(self) -> None:
        """Handle the button press."""
        await self.coordinator.async_get_pairing()


class WiSafe2StartPairingButton(WiSafe2ButtonBase):
    """Start Pairing button."""

    _attr_name = "Start Pairing Mode"
    _attr_icon = "mdi:link-plus"
    _attr_entity_category = EntityCategory.CONFIG

    def __init__(
        self,
        coordinator: WiSafe2Coordinator,
        config_entry: ConfigEntry,
    ) -> None:
        """Initialize the button."""
        super().__init__(coordinator, config_entry)
        self._attr_unique_id = f"{config_entry.entry_id}_start_pairing"

    async def async_press(self) -> None:
        """Handle the button press."""
        await self.coordinator.async_start_pairing()
