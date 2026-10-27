# Changelog

## Unreleased

### Test firmware (FireAngelNanoTest 1.0.0)

- Add `Arduino-Firmware/FireAngelNanoTest`, a serial console to the radio
  module for protocol experiments, not for Home Assistant.
- Send nothing at startup; the init `D3 19 50 00` is an explicit command.
- Escape `7E`/`7D` in frames to the radio module and repeat frames it does not
  clock for up to 60 s.
- Answer every `41` with `91`, and `C1`, `C3` and `72`/`C5`/`C7` for the own
  device ID; mark copies of network messages.
- Acknowledge radio bytes in a pin-change interrupt.
- Keep the protocol logic in `w2_protocol.cpp` with host tests in `tests/`.
- Document a hardware issue found with it: after every reset of the Nano the
  first frame to the radio module is flooded through the network as a radio
  message (see `docs/firmware-variants.md`).
- Require a 1 kΩ pull-down from D9 (IRQ) to GND; with it, resets leave nothing
  on the bus and the first frame is handled normally (tested three times).

### Firmware folder layout

- Move all sketches into `Arduino-Firmware/` with matching sketch directories:
  `FireAngelNanoLegacy/FireAngelNanoLegacy.ino` (was `Arduino/FireAngelNano.ino`),
  `FireAngelNanoV2/FireAngelNanoV2.ino` (was `Arduino-V2/FireAngelNanoV2.ino`) and
  `FireAngelNanoDebug/FireAngelNanoDebug.ino` (was `Arduino-Debug/FireAngelNano.ino`).

### V2 emergency commands (firmware 2.1.0)

- Add `co_emergency` and `fire_emergency` to the V2 firmware; they send the
  same `0x50` alarm frames as legacy commands `4~`/`5~`.
- Add `co_emergency_stop` and `fire_emergency_stop`, which send the `0x51`
  alarm stop known from FireAngel's Mesh Monitor software (not yet verified).

### Reviewer hardening

- Extract duplicated SPI, transaction, watchdog-wait, diagnostic, and decoder
  logic into the shared `WiSafeRadioCore` Arduino library.
- Prevent legacy init retry dots from contaminating direct/raw output.
- Drain and reject serial commands as busy during pairing instead of allowing
  the UART ring buffer to overflow silently.
- Document the deliberately bounded V2 command parser, remove the unused SPI
  variable, and reuse the common hexadecimal printer.

### Clarify bridge and detector test semantics

- Preserve the legacy bridge-device synthetic `PASS` as a record that the
  bridge initiated a sound command, not as a response from a detector.
- Rename V2 outgoing commands to `sound_fire`, `sound_co`, and
  `sound_combined` so they cannot be mistaken for detector verification.
- Document that genuine test events come from separately pressing each
  detector's physical test button; remote signals produce no detector reply.

### Firmware split — legacy-only and V2-only

- Restore `Arduino/FireAngelNano.ino` to the improved Stage 3 legacy protocol.
- Add `Arduino-V2/FireAngelNanoV2.ino` as a separate protocol-2-only image.
- Make the V2 image announce its firmware and protocol automatically at startup.
- Remove protocol negotiation, legacy fallback, and the runtime legacy command
  from the V2 image.
- Document passive Home Assistant protocol detection and both flashing choices.

### Stage 6 — deferred recommendations

- Document evidence-gated reset diagnostics, radio recovery, CI, raw-frame,
  EEPROM identity, and watchdog-count recommendations without implementing
  optional firmware behavior.

### Stage 5 — Home Assistant migration point

- Document the integration changes needed for passive detection, typed messages,
  command correlation, diagnostics, and three-way firmware compatibility.
- Make no changes to the separate Home Assistant integration repository.

### Stage 4 — serial protocol v2

- Add a dedicated V2-only firmware with an unsolicited startup identification message.
- Add newline-delimited JSON events, heartbeats, status, errors, and correlated command results.
- Report command transmission acceptance separately from genuine detector test results.
- Expose firmware/protocol versions, uptime, radio state, and diagnostic counters.
- Keep direct/raw mode unchanged and do not expose emergency-simulation commands in v2.
- Document the complete v2 wire protocol and compatibility behaviour.

### Stage 3 — protocol model and diagnostics

- Decode supported radio frames into a fixed-size `RadioEvent` model before serialising legacy output.
- Preserve raw message, event, and status bytes in the decoded representation.
- Track unknown frame types, malformed frames, overflow, incomplete-frame timeouts, command timeouts/retries, and radio initialisation retries.
- Retain the legacy `FIRE` abstraction for both `0x81` and `0x82`; available notes and captures do not support inventing a smoke/heat distinction.
- Preserve the exact legacy event schema, strings, and command behaviour.

### Stage 2 — reception reliability

- Replace loop-count incomplete-frame expiry with rollover-safe elapsed time.
- Service and dispatch unsolicited SPI frames during command response, retry, test-result, and pairing waits.
- Replace command `goto` retries with bounded transaction helpers that validate fresh responses.
- Separate SPI acquisition, frame dispatch, and command transactions into dedicated helpers.
- Enable the AVR watchdog with a two-second timeout after successful startup.
- Keep all legacy command bytes, responses, JSON events, heartbeat, and raw mode behaviour.

### Stage 1 — correctness and memory safety

- Bound SPI and serial frame acquisition and safely discard overlong frames.
- Validate frame lengths before decoding or checking command responses.
- Require both bytes of the radio initialisation acknowledgement to match.
- Clear transaction state before every command attempt so retries wait for a fresh response.
- Add internal counters for SPI overflow and malformed frames.
- Preserve the legacy serial protocol and Home Assistant compatibility.
