# WiSafe2 bridge serial protocol v2

Protocol v2 is implemented by the dedicated
`Arduino-V2/FireAngelNanoV2.ino` firmware. It is not a runtime mode of the
legacy firmware. Serial settings remain 115200 baud, 8 data bits, no parity,
one stop bit.

In normal mode, both directions use UTF-8/ASCII JSON objects terminated by LF
(`0x0A`). CR before LF is accepted. Input is limited to 79 bytes excluding
CR/LF. Command names and string values are case-sensitive.

The hardware direct/raw jumper remains supported. When selected, the firmware
uses the original raw `0x7E` framing and does not emit or accept protocol-v2
JSON.

## Startup identification

After the radio initializes, every normal-mode boot emits:

```json
{"type":"bridge","event":"startup","firmware":"2.0.0","protocol":2,"radio":"ready"}
```

This unsolicited message tells Home Assistant that the board is running the
V2-only image. No negotiation command is sent or accepted. There is no command
that changes this firmware into legacy mode.

Opening an Arduino Nano serial port commonly resets the board, but hosts must
not rely on that behavior. If the startup line was missed, any later typed v2
heartbeat, event, status, command result, or error also identifies the protocol
as v2.

If radio initialization ultimately fails, the firmware emits a JSON error
before resetting:

```json
{"type":"error","code":"radio_init_failed"}
```

## Common fields

- `type`: `bridge`, `heartbeat`, `event`, `command_result`, `status`, or
  `error`.
- `id`: optional unsigned 16-bit request ID copied from a command.
- `device`: uppercase six-digit WiSafe2 device ID.
- `model`: uppercase four-digit model ID when present in the radio frame.
- `raw_status`: unsigned original status byte; unknown bits are preserved.
- `radio`: `ready` or `not_ready`.
- `uptime`: Arduino `millis()` value in milliseconds.

Consumers must ignore unknown fields and enum values. Numbers are JSON numbers,
not quoted strings.

## Heartbeat and status

Heartbeat interval is 25 seconds:

```json
{"type":"heartbeat","uptime":123456,"radio":"ready"}
```

Request status with:

```json
{"command":"status","id":17}
```

Example response:

```json
{"type":"status","id":17,"firmware":"2.0.0","protocol":2,"uptime":123456,"radio":"ready","diagnostics":{"overflow":0,"malformed":0,"incomplete":0,"unknown":0,"command_timeout":0,"command_retry":0,"radio_reinit":0}}
```

Diagnostic counters are unsigned 16-bit counters, wrap naturally, and reset at
boot.

## Received radio events

These messages report traffic received from the WiSafe2 network. They are not
synthesized command results.

```json
{"type":"event","device":"92BF1A","model":"ED08","event":"FIRE_TEST","result":"PASS","base":"ON","battery":"OK","raw_status":1}
{"type":"event","device":"92BF1A","model":"7803","event":"CO_TEST","result":"FAIL","base":"ON","raw_status":0}
{"type":"event","device":"92BF1A","model":"ED08","event":"STATUS","base":"ON","battery":"LOW","raw_status":71}
{"type":"event","device":"92BF1A","event":"FIRE_EMERGENCY","base":"ON","raw_status":0}
{"type":"event","device":"92BF1A","event":"CO_EMERGENCY","base":"ON","raw_status":0}
{"type":"event","device":"92BF1A","event":"SILENCE","base":"ON","raw_status":1}
{"type":"event","device":"92BF1A","event":"MISSING","base":"MISSING","battery":"MISSING","raw_status":0}
```

Known values `0x81` and `0x82` both remain in the `FIRE` family. Available
captures do not establish a reliable smoke-versus-heat distinction. Unknown
test and emergency subtypes are emitted as `TEST` and `EMERGENCY`.

## Commands and correlation

Commands are compact JSON lines:

```json
{"command":"fire_test","id":17}
```

`id` is optional and, when present, is an integer from 0 through 65535.
Supported commands are:

- `co_test`
- `fire_test`
- `combined_test`
- `silence_co`
- `silence_fire`
- `pairing_state`
- `pairing`
- `status`

Emergency simulation is deliberately not exposed.

A successfully acknowledged/transmitted test request produces:

```json
{"type":"command_result","id":17,"command":"fire_test","result":"accepted"}
```

`accepted` does not mean a detector passed. Only a later, independently
received `type: event` test message with `result: PASS` is a detector test
result.

General commands return `accepted` or `timeout`. `pairing_state` returns
`paired`, `unpaired`, or `timeout`. `pairing` returns `accepted`, followed after
the pairing window by `paired` or `unpaired`; it may instead return
`already_paired` or `timeout`. Unsolicited events can appear between a command
and its result and must be processed normally.

## Errors

```json
{"type":"error","id":17,"code":"unknown_command"}
{"type":"error","code":"serial_frame_overflow"}
```

Defined codes are `malformed_command`, `invalid_id`, `unknown_command`,
`serial_frame_overflow`, `pairing_state_timeout`, and `radio_init_failed`.
Malformed input is discarded at its LF boundary so the next line can
resynchronize.

## Example transcript

The first line is unsolicited. `>` is host input and `<` is firmware output.

```text
< {"type":"bridge","event":"startup","firmware":"2.0.0","protocol":2,"radio":"ready"}<LF>
> {"command":"fire_test","id":17}<LF>
< {"type":"command_result","id":17,"command":"fire_test","result":"accepted"}<LF>
< {"type":"event","device":"92BF1A","model":"ED08","event":"FIRE_TEST","result":"PASS","base":"ON","battery":"OK","raw_status":1}<LF>
> {"command":"status","id":18}<LF>
< {"type":"status","id":18,"firmware":"2.0.0","protocol":2,"uptime":123456,"radio":"ready","diagnostics":{"overflow":0,"malformed":0,"incomplete":0,"unknown":0,"command_timeout":0,"command_retry":0,"radio_reinit":0}}<LF>
```

## Relationship to legacy firmware

The improved legacy-only image is `Arduino/FireAngelNano.ino`. It preserves the
original command bytes, `0x7E` framing, heartbeat, textual responses, and legacy
JSON. Choosing between protocols is therefore a firmware-flashing decision,
not a serial session setting. Home Assistant should parse both, detect V2 from
its typed startup/output messages, and never send a protocol probe.
