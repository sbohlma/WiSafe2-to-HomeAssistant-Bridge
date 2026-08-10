# Changelog

## Unreleased

### Stage 5 — Home Assistant migration point

- Document the integration changes needed for negotiation, typed messages,
  command correlation, diagnostics, and three-way firmware compatibility.
- Make no changes to the separate Home Assistant integration repository.

### Stage 4 — serial protocol v2 with legacy compatibility

- Add opt-in serial protocol v2 negotiation while retaining legacy mode after every reset.
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
