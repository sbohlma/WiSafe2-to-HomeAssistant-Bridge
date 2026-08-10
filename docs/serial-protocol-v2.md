# WiSafe2 bridge serial protocol v2

Firmware `2.0.0` retains the original serial interface and adds an opt-in,
newline-delimited JSON protocol. Serial settings remain 115200 baud, 8 data
bits, no parity, one stop bit.

## Negotiation and compatibility

The bridge starts in legacy mode after every reset. In normal (non-direct)
mode, send these three bytes to opt in:

```text
V2~
```

`~` above means the byte `0x7E`, not a newline. A successful negotiation
returns one JSON line:

```json
{"type":"bridge","event":"ready","firmware":"2.0.0","protocol":2,"radio":"ready"}
```

After that response, both directions use UTF-8/ASCII JSON objects terminated
by LF (`0x0A`). CR before LF is accepted. JSON input is limited to 79 bytes
excluding CR/LF. Object member order is unrestricted and horizontal whitespace
around values is accepted. Command names and string values are case-sensitive.

To return to legacy mode without resetting, send:

```json
{"command":"legacy","id":9}
```

The bridge acknowledges that command in v2 before changing mode. Direct/raw
jumper mode never negotiates v2; it retains byte-for-byte raw operation.

## Common fields

- `type`: message category: `bridge`, `heartbeat`, `event`, `command_result`,
  `status`, or `error`.
- `id`: optional unsigned 16-bit request ID copied from a command.
- `device`: uppercase, six-digit WiSafe2 device ID.
- `model`: uppercase, four-digit model ID when present in the radio frame.
- `raw_status`: unsigned original status byte. Unknown bits are not discarded.
- `radio`: `ready` or `not_ready`.
- `uptime`: rollover-capable Arduino `millis()` value in milliseconds.

Consumers must ignore unknown fields and unknown enum values for forward
compatibility. Numbers are JSON numbers, not quoted strings.

## Bridge, heartbeat, and status

The negotiation response is the `bridge`/`ready` message shown above.
Heartbeat interval remains 25 seconds:

```json
{"type":"heartbeat","uptime":123456,"radio":"ready"}
```

Request status and diagnostics with:

```json
{"command":"status","id":17}
```

Example response:

```json
{"type":"status","id":17,"firmware":"2.0.0","protocol":2,"uptime":123456,"radio":"ready","diagnostics":{"overflow":0,"malformed":0,"incomplete":0,"unknown":0,"command_timeout":0,"command_retry":0,"radio_reinit":0}}
```

Diagnostic counters are unsigned 16-bit counters and wrap naturally. They are
reset when the bridge resets.

## Received radio events

All of these are reports received from the WiSafe2 network, not locally
synthesised command results.

```json
{"type":"event","device":"92BF1A","model":"ED08","event":"FIRE_TEST","result":"PASS","base":"ON","battery":"OK","raw_status":1}
{"type":"event","device":"92BF1A","model":"7803","event":"CO_TEST","result":"FAIL","base":"ON","raw_status":0}
{"type":"event","device":"92BF1A","model":"ED08","event":"STATUS","base":"ON","battery":"LOW","raw_status":71}
{"type":"event","device":"92BF1A","event":"FIRE_EMERGENCY","base":"ON","raw_status":0}
{"type":"event","device":"92BF1A","event":"CO_EMERGENCY","base":"ON","raw_status":0}
{"type":"event","device":"92BF1A","event":"SILENCE","base":"ON","raw_status":1}
{"type":"event","device":"92BF1A","event":"MISSING","base":"MISSING","battery":"MISSING","raw_status":0}
```

Known test event values `0x81` and `0x82` both remain `FIRE_TEST`. Existing
captures do not establish a reliable smoke-versus-heat distinction. An unknown
fire/test subtype is emitted as `TEST`; an unknown emergency subtype is emitted
as `EMERGENCY`.

## Commands

Commands have the form:

```json
{"command":"fire_test","id":17}
```

`id` is optional and, when present, must be an integer from 0 through 65535.
The response repeats it. Supported v2 command names are:

- `co_test`
- `fire_test`
- `combined_test`
- `silence_co`
- `silence_fire`
- `pairing_state`
- `pairing`
- `status`
- `legacy`

Emergency simulation is deliberately not exposed in protocol v2.

A successfully acknowledged/transmitted test request produces:

```json
{"type":"command_result","id":17,"command":"fire_test","result":"accepted"}
```

This does **not** mean a detector passed its test. Only a later, independently
received `event` message whose `event` is `FIRE_TEST`, `CO_TEST`, or `TEST` and
whose `result` is `PASS` is a detector test result.

Other command results are:

- General commands: `accepted` or `timeout`.
- `pairing_state`: `paired`, `unpaired`, or `timeout`.
- `pairing`: `accepted`, followed after the pairing window by `paired` or
  `unpaired`; it may instead return `already_paired` or `timeout`.

Unsolicited radio events can appear between a command and its result and must be
processed normally.

## Errors

Errors are JSON lines such as:

```json
{"type":"error","id":17,"code":"unknown_command"}
{"type":"error","code":"serial_frame_overflow"}
```

Defined codes are `malformed_command`, `unknown_command`,
`serial_frame_overflow`, and `pairing_state_timeout`. A malformed input line is
discarded at its LF boundary, allowing the next line to resynchronise.

## Differences from the legacy protocol

- V2 is opt-in; legacy remains the reset default.
- V2 uses LF-delimited JSON in both directions instead of single command bytes
  and `0x7E` framing.
- Every output has a `type`; field names and enum values are stable and
  machine-oriented.
- Heartbeats contain uptime and radio state instead of a cycling digit string.
- Command results can carry correlation IDs.
- Command acceptance is distinct from genuine detector test `PASS` events.
- Firmware version, protocol version, uptime, radio state, and counters are
  queryable.
- V2 does not expose the legacy emergency-simulation command bytes.
- Raw/direct jumper mode is unchanged and cannot enter v2.

The legacy output, heartbeat, command bytes `0x31` through `0x39`, textual
responses, and synthetic post-command test messages remain unchanged in legacy
mode for compatibility with the existing Home Assistant integration.

## Example transcript

`>` denotes bytes sent by the host and `<` denotes one bridge output line.

```text
> V2<0x7E>
< {"type":"bridge","event":"ready","firmware":"2.0.0","protocol":2,"radio":"ready"}
> {"command":"fire_test","id":17}<LF>
< {"type":"command_result","id":17,"command":"fire_test","result":"accepted"}
< {"type":"event","device":"92BF1A","model":"ED08","event":"FIRE_TEST","result":"PASS","base":"ON","battery":"OK","raw_status":1}
> {"command":"status","id":18}<LF>
< {"type":"status","id":18,"firmware":"2.0.0","protocol":2,"uptime":123456,"radio":"ready","diagnostics":{"overflow":0,"malformed":0,"incomplete":0,"unknown":0,"command_timeout":0,"command_retry":0,"radio_reinit":0}}
> {"command":"legacy","id":19}<LF>
< {"type":"command_result","id":19,"command":"legacy","result":"accepted"}
```
