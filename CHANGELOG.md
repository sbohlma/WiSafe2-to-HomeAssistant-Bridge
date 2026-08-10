# Changelog

## Unreleased

### Stage 1 — correctness and memory safety

- Bound SPI and serial frame acquisition and safely discard overlong frames.
- Validate frame lengths before decoding or checking command responses.
- Require both bytes of the radio initialisation acknowledgement to match.
- Clear transaction state before every command attempt so retries wait for a fresh response.
- Add internal counters for SPI overflow and malformed frames.
- Preserve the legacy serial protocol and Home Assistant compatibility.
