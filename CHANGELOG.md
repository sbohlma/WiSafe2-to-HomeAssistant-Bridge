# Changelog

## Unreleased

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
