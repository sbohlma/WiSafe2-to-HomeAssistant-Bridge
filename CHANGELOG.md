# Changelog

## Unreleased

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
