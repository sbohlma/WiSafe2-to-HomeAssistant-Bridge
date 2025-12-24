"""WiSafe2 FireAngel Bridge integration for Home Assistant."""
from __future__ import annotations

import logging
from typing import Any

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant
from homeassistant.helpers import device_registry as dr

from .const import (
    DOMAIN,
    CONF_SERIAL_PORT,
    CONF_BAUD_RATE,
    DEFAULT_BAUD_RATE,
    PLATFORMS,
)
from .coordinator import WiSafe2Coordinator

_LOGGER = logging.getLogger(__name__)

PLATFORMS_LIST: list[Platform] = [
    Platform.SENSOR,
    Platform.BINARY_SENSOR,
    Platform.BUTTON,
]


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up WiSafe2 FireAngel Bridge from a config entry."""
    hass.data.setdefault(DOMAIN, {})

    # Create coordinator
    coordinator = WiSafe2Coordinator(
        hass,
        entry.data[CONF_SERIAL_PORT],
        entry.data.get(CONF_BAUD_RATE, DEFAULT_BAUD_RATE),
    )

    # Start serial connection
    if not await coordinator.async_start():
        return False

    # Add configured devices
    devices = entry.options.get("devices", [])
    for device_config in devices:
        coordinator.add_device(
            device_id=device_config["device_id"],
            model_id=device_config.get("model"),
            name=device_config.get("name"),
        )

    # Store coordinator
    hass.data[DOMAIN][entry.entry_id] = coordinator

    # Register the bridge device
    device_registry = dr.async_get(hass)
    device_registry.async_get_or_create(
        config_entry_id=entry.entry_id,
        identifiers={(DOMAIN, "bridge")},
        manufacturer="FireAngel / DIY",
        model="WiSafe2 Bridge",
        name="WiSafe2 Bridge",
        sw_version="1.0.0",
    )

    # Register alarm devices
    for device_id, device in coordinator.devices.items():
        device_registry.async_get_or_create(
            config_entry_id=entry.entry_id,
            identifiers={(DOMAIN, device_id)},
            manufacturer="FireAngel",
            model=device.name or "Unknown Model",
            name=device.name or f"WiSafe2 Alarm {device_id}",
            via_device=(DOMAIN, "bridge"),
        )

    # Set up platforms
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS_LIST)

    # Listen for options updates
    entry.async_on_unload(entry.add_update_listener(async_update_options))

    # Perform first refresh
    await coordinator.async_refresh()

    return True


async def async_update_options(hass: HomeAssistant, entry: ConfigEntry) -> None:
    """Update options."""
    await hass.config_entries.async_reload(entry.entry_id)


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload a config entry."""
    # Unload platforms
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS_LIST)

    if unload_ok:
        # Stop coordinator
        coordinator: WiSafe2Coordinator = hass.data[DOMAIN][entry.entry_id]
        await coordinator.async_stop()
        hass.data[DOMAIN].pop(entry.entry_id)

    return unload_ok


async def async_migrate_entry(hass: HomeAssistant, config_entry: ConfigEntry) -> bool:
    """Migrate old entry."""
    _LOGGER.debug("Migrating from version %s", config_entry.version)

    if config_entry.version == 1:
        # Future migrations go here
        pass

    return True
