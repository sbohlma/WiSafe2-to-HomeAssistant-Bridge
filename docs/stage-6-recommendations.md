# Stage 6 recommendations

These are deliberately deferred. None is required for protocol v2 or the Home
Assistant migration, and none should be enabled before the Stage 1–5 firmware
has been exercised on the installed bridge and a real alarm network.

## Recommended next, after hardware observation

### Capture reset cause

Capture `MCUSR` at the earliest safe startup point and expose decoded
power-on, brown-out, external, and watchdog reset flags in v2 status. This is
small and useful for distinguishing a watchdog recovery from USB/power resets.
Clear the flags correctly before enabling the watchdog to avoid reset loops.

Do not write every reset to EEPROM. EEPROM wear is not justified unless a
durable reset count proves operationally necessary; the reset cause for the
current boot is usually sufficient.

### Radio recovery policy

Observe the new `command_timeout` and `command_retry` counters first. If real
use demonstrates that the radio becomes persistently unresponsive, add a
conservative recovery state that re-runs the already-known initialisation
command after several consecutive failures and while no frame is in progress.

This requires hardware testing because radio reinitialisation might alter
timing or state on the physical WiSafe2 network. It must never run in response
to one lost command response, and incoming alarm traffic must cancel/defer it.
Do not add a new RF reset sequence without captured evidence.

### Build verification

Add CI that compiles `Arduino/FireAngelNano.ino` for
`arduino:avr:nano:cpu=atmega328old` with warnings enabled and reports flash and
static RAM use. Because the sketch directory name differs from the main `.ino`
name, the job should copy it into a temporary `FireAngelNano/FireAngelNano.ino`
directory before invoking `arduino-cli`.

Set a conservative static-RAM budget (for example, fail above 75%) so future
features leave stack space. Treat sketch warnings as actionable while filtering
the known unused-parameter warnings emitted by the Arduino AVR core itself.

### Protocol constants and host-side decoder tests

Continue replacing magic radio values with scoped constants as protocol
knowledge becomes evidence-backed. Keep raw values alongside decoded enums.
Extract the pure decode rules into a host-testable C++ unit only if that can be
done without duplicating AVR code or increasing firmware complexity.

Use recorded byte frames for tests covering every known type, minimum lengths,
truncation, unknown values, and `millis()` rollover. Captures are evidence; test
names must not claim smoke/heat semantics that the captures do not establish.

## Useful only on demonstrated demand

### Opt-in raw frame diagnostics

An explicit v2 diagnostic command could stream the next unknown frame as
bounded hexadecimal data. It should be off by default, rate-limited, and never
replace normal event delivery. Avoid always emitting raw frames: serial output
can itself consume time during alarm bursts and expose undocumented data that
consumers may accidentally treat as stable.

### Configurable bridge identity

EEPROM-backed device ID/model configuration is feasible but not presently
necessary. The embedded identity is compatible with existing deployments and
changing it affects how the bridge appears on the WiSafe2 network and in Home
Assistant.

Only add this if multiple bridges or ID collisions create a concrete need. A
future configuration command would need strict length/range validation, an
EEPROM schema/version marker, write-on-change behavior, read-back confirmation,
and a documented factory-default recovery path. It must not permit arbitrary RF
frames or emergency simulation.

### Persistent watchdog count

A watchdog reset count in EEPROM is lower value than the current reset cause
and carries wear/corruption considerations. Prefer HA-side observation of reset
events. Revisit only if field diagnosis shows resets that cannot otherwise be
captured.

## Not recommended now

- Moving device IDs or all settings to EEPROM merely because space exists.
- Emitting every unknown/raw radio frame continuously.
- Automatic radio or MCU resets after isolated command failures.
- Adding Wi-Fi, MQTT, ESPHome, HA logic, or emergency simulation.
- Renaming `0x81`/`0x82` as smoke/heat without new controlled captures.
- Removing legacy serial mode during the protocol v2 migration window.

The safest next step is real-hardware soak testing of Stages 1–4, followed by
the HA integration update in the Stage 5 plan. Diagnostic observations from
that period should decide which, if any, deferred feature is worth its added
complexity.
