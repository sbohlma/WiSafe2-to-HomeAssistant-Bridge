# WiSafe2 bridge serial protocol v2

Protocol v2 is implemented by the dedicated
`Arduino-Firmware/FireAngelNanoV2/FireAngelNanoV2.ino` firmware. It is not a runtime mode of the
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

The heartbeat is an idle keepalive: it is emitted after approximately 25
seconds without other radio/management activity. Any valid message is therefore
evidence of bridge activity; hosts should not require a heartbeat while events
or command results are flowing.

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

A test event is received when the originating detector's physical test button
is pressed. That detector transmits the event to the other WiSafe2 devices,
including the bridge, and `device` identifies the detector whose button was
pressed. Recording every detector therefore requires pressing each detector's
button separately.

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
{"command":"sound_fire","id":17}
```

`id` is optional and, when present, is an integer from 0 through 65535.
Supported commands are:

- `sound_co`
- `sound_fire`
- `sound_combined`
- `co_emergency`
- `fire_emergency`
- `co_emergency_stop`
- `fire_emergency_stop`
- `silence_co`
- `silence_fire`
- `pairing_state`
- `pairing`
- `status`

### Emergency commands

`co_emergency` and `fire_emergency` transmit a **real alarm** (`50 dev 41|81 00`)
under the bridge device ID. Every detector in the WiSafe2 network sounds its
alarm; this is not a test signal. `co_emergency_stop` and `fire_emergency_stop`
transmit the alarm stop (`51 dev 41|81`) used by FireAngel's Mesh Monitor
software; whether detectors end the alarm on it has not been verified yet.
`silence_co`/`silence_fire` remain available. All four return `accepted` or
`timeout` like the other commands; `accepted` only means the donor radio took
the frame. Hosts should require an explicit user confirmation before sending
an emergency command.

A successfully accepted bridge-originated sound request produces:

```json
{"type":"command_result","id":17,"command":"sound_fire","result":"accepted"}
```

`accepted` refers only to the attached donor radio and the transmission
process. The remote detectors sound, but they do not reply to the bridge. It
does not confirm RF reception, exercise an individual detector on demand, or
mean any detector passed. There is no known command that remotely tests every
detector and collects results.

Only an independently received `type: event` test message caused by pressing a
detector's own button is recorded as that detector's test report.

In the legacy-only firmware, the equivalent initiation record is a synthetic
test `PASS` using the configured bridge device ID. Protocol V2 represents that
bridge action as `command_result: accepted` instead, avoiding confusion with a
received detector event.

General commands return `accepted` or `timeout`. `pairing_state` returns
`paired`, `unpaired`, or `timeout`. `pairing` returns `accepted`, followed after
the pairing window by `paired` or `unpaired`; it may instead return
`already_paired` or `timeout`. Unsolicited events can appear between a command
and its result and must be processed normally.

Only one management command may run at a time. During the approximately
21-second pairing window, the firmware continues processing radio events but
rejects additional serial commands with `code: busy`. Hosts should serialize
commands and wait for pairing to finish.

## Errors

```json
{"type":"error","id":17,"code":"unknown_command"}
{"type":"error","code":"serial_frame_overflow"}
```

Defined codes are `malformed_command`, `invalid_id`, `unknown_command`, `busy`,
`serial_frame_overflow`, `pairing_state_timeout`, and `radio_init_failed`.
Malformed input is discarded at its LF boundary so the next line can
resynchronize.

## Example transcript

The first line is unsolicited. `>` is host input and `<` is firmware output.

```text
< {"type":"bridge","event":"startup","firmware":"2.0.0","protocol":2,"radio":"ready"}<LF>
> {"command":"sound_fire","id":17}<LF>
< {"type":"command_result","id":17,"command":"sound_fire","result":"accepted"}<LF>
< {"type":"event","device":"92BF1A","model":"ED08","event":"FIRE_TEST","result":"PASS","base":"ON","battery":"OK","raw_status":1}<LF>
> {"command":"status","id":18}<LF>
< {"type":"status","id":18,"firmware":"2.0.0","protocol":2,"uptime":123456,"radio":"ready","diagnostics":{"overflow":0,"malformed":0,"incomplete":0,"unknown":0,"command_timeout":0,"command_retry":0,"radio_reinit":0}}<LF>
```

## Relationship to legacy firmware

The improved legacy-only image is
`Arduino-Firmware/FireAngelNanoLegacy/FireAngelNanoLegacy.ino`. It preserves the
original command bytes, `0x7E` framing, heartbeat, textual responses, and legacy
JSON. Choosing between protocols is therefore a firmware-flashing decision,
not a serial session setting. Home Assistant should parse both, detect V2 from
its typed startup/output messages, and never send a protocol probe.
