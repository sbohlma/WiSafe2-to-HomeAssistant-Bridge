# Home Assistant support for legacy and V2 firmware

This is the implementation plan for
[`NikNakk/fireangel-pro-connected-component`](https://github.com/NikNakk/fireangel-pro-connected-component),
based on repository commit `46bf7b2` (2026-08-10). No changes to that repository
are included here.

The integration should support:

| Firmware | Protocol | Required result |
| --- | --- | --- |
| Original C19HOP | Legacy | Fully supported |
| Improved `Arduino-Firmware/FireAngelNanoLegacy/` image | Legacy | Fully supported |
| `Arduino-Firmware/FireAngelNanoV2/` image | Protocol 2 | Preferred structured protocol |

The V2 wire format is defined in [serial-protocol-v2.md](serial-protocol-v2.md).

## Detection: listen, do not negotiate

Add protocol state in `bridge.py`: `UNKNOWN`, `LEGACY`, or `V2`. On every
serial connection/reconnection, start in `UNKNOWN` and immediately start the
existing asynchronous line reader. Do not write probe or negotiation bytes.

The V2-only board identifies itself after radio initialization:

```json
{"type":"bridge","event":"startup","firmware":"2.0.0","protocol":2,"radio":"ready"}
```

Select V2 when that message arrives. Also select V2 on any valid JSON object
with a recognized v2 `type`, which covers attaching after the startup line was
missed.

Select legacy on a legacy `heartBeat` object, an untyped detector object, or a
recognized legacy startup/text response such as `INIT OK`. Continue processing
the line that established the mode; it may be a real alarm event.

Opening a Nano serial port commonly resets it, but the integration must not
depend on DTR reset timing. If no line has arrived, leave the protocol unknown
and command buttons unavailable rather than guessing or sending a probe that
could contaminate the other parser. A heartbeat provides identification within
25 seconds on a quiet connection.

If typed V2 traffic appears while marked legacy, or clearly legacy traffic
appears while marked V2, treat it as a board replacement/reset: cancel pending
command futures, switch mode, and process the new line. Redetect after every
reconnect.

## Bridge runtime model

Keep serial ownership and dispatch in
`custom_components/fireangel_pro_connected/bridge.py`. Suggested fields:

- `protocol_mode`
- `protocol_version: int | None`
- `firmware_version: str | None`
- `radio_state: str | None`
- `bridge_uptime: int | None`
- `diagnostic_counters: dict[str, int]`
- `last_error: str | None`
- `last_command_result: dict[str, Any] | None`
- `_next_request_id: int`
- `_pending_commands: dict[int, asyncio.Future[dict[str, Any]]]`
- `_write_lock: asyncio.Lock`

Keep detector/entity logic independent of the wire protocol. Decode both
formats into the existing `DetectorState` update path.

## Typed V2 dispatch

In `async_process_line()`, dispatch on `payload.get("type")` when it is a
string. If `type` is absent, run the existing legacy parser.

- `bridge`: validate protocol 2 and store firmware/radio metadata.
- `heartbeat`: update `last_heartbeat`, uptime, and radio state.
- `status`: update version, uptime, radio, and validated counters.
- `event`: this is the only V2 type passed to detector discovery/state updates.
- `command_result`: resolve the matching pending request; never update a
  detector.
- `error`: fail a matching request when an ID exists, retain diagnostics, and
  continue reading.
- Unknown types: retain as `last_message`, log at debug level, and ignore.

Malformed messages, wrong field types, unknown enums, late/duplicate results,
and unfamiliar text must not terminate the reader. Ignore unknown fields for
forward compatibility.

## Event normalization

Translate V2 event names at the protocol boundary to preserve current entity
states and automations:

| V2 event | Existing internal value |
| --- | --- |
| `FIRE_TEST` | `FIRE TEST` |
| `CO_TEST` | `CARBON MONOXIDE TEST` |
| `TEST` | `TEST` |
| `FIRE_EMERGENCY` | `FIRE EMERGENCY` |
| `CO_EMERGENCY` | `CARBON MONOXIDE EMERGENCY` |
| `EMERGENCY` | `EMERGENCY` |
| `STATUS` | Update base/battery; do not replace the last event |
| `SILENCE` | `SILENCE` |
| `MISSING` | `MISSING` |

Continue current device/model normalization and partial update merging. Preserve
`raw_status` as optional diagnostic data without inferring unknown semantics.
Continue conservative smoke/heat handling for `0x81` and `0x82`.

Only a V2 `type: event` test with `result: PASS` may update
`last_test_pass`. A `command_result` of `accepted` means only that the bridge
radio accepted/transmitted a sound request. Remote detectors do not reply.

For legacy input, update detector `last_test_pass` only for a
detector-originated test event. Both the original C19HOP firmware and the
improved legacy firmware emit a synthetic `PASS` under the configured bridge
device ID after a host command. Preserve it as the bridge's initiation/action
record, but never treat it as a detector response or physical detector result.
It is semantically equivalent to V2 `command_result: accepted`.

Each detector must be tested by pressing its own physical test button. The
resulting WiSafe2 event identifies that detector and can update its timestamp.
Do not present bridge-originated sound commands as “test all,” successful
detector tests, or evidence that any detector received the RF signal.

## Sending commands

Use semantic command keys with an encoding table:

| Semantic command | Legacy bytes | V2 command |
| --- | --- | --- |
| Sound CO test signal | `b"1~"` | `sound_co` |
| Sound fire test signal | `b"2~"` | `sound_fire` |
| Sound combined test signal | `b"3~"` | `sound_combined` |
| CO emergency (real alarm) | `b"4~"` | `co_emergency` |
| Fire emergency (real alarm) | `b"5~"` | `fire_emergency` |
| Stop CO emergency | – | `co_emergency_stop` |
| Stop fire emergency | – | `fire_emergency_stop` |
| Silence as CO | `b"6~"` | `silence_co` |
| Silence as fire | `b"7~"` | `silence_fire` |
| Pairing state | `b"8~"` | `pairing_state` |
| Start pairing | `b"9~"` | `pairing` |

Emergency commands sound a real alarm on every detector. Expose them only
behind an explicit confirmation and never from automations by default.

Update the three Home Assistant button labels to say “Sound … test signal,”
not “Test …” or “Test all.” Their success feedback means command acceptance
only. It must not update a detector entity or `last_test_pass`.

For legacy mode, preserve the exact bytes and fire-and-forget behavior. For V2,
allocate an unused 16-bit ID, register a future before writing, encode compact
JSON with `separators=(",", ":")`, append LF, and drain. Use an HA-side timeout
and remove the future in `finally`. Process unsolicited events normally while a
command is pending.

Until protocol mode is known, disable or reject command button presses with a
clear “waiting for firmware identification” error. Do not guess legacy.

Pairing returns `accepted` first and later `paired`/`unpaired` with the same ID.
A button press may complete on `accepted`; retain the later result as diagnostic
state. `pairing_state` completes on `paired`/`unpaired`. Serialize management
commands while pairing is active. If a V2 command is nevertheless sent, handle
the correlated `busy` error normally; legacy firmware returns `CMD BUSY`.

## Heartbeat, availability, and diagnostics

Treat every valid recognized message as activity, including legacy `heartBeat`
and V2 `type: heartbeat`. The firmware heartbeat is an idle keepalive and may be
postponed by events or commands. Preserve the serial connection sensor and base
availability on `last_activity`, using a timeout comfortably above the
25-second quiet interval (approximately 60–75 seconds).

Extend `diagnostics.py` with detected mode, versions, radio state, last
heartbeat, last error/result, and firmware counters. Optionally expose firmware
version and radio state as disabled-by-default diagnostic sensors. Avoid one HA
entity per counter unless operational need is demonstrated.

## File-level changes

- `bridge.py`: passive detection, protocol state, V2 dispatch, correlation IDs,
  semantic command encoder, and bridge metadata.
- `const.py`: protocol enums/constants and legacy/V2 command mappings; remove
  any proposed negotiation bytes.
- `button.py`: send semantic commands and show unavailable while mode is
  unknown; preserve entity unique IDs.
- `diagnostics.py`: detected protocol and V2 status/error data.
- `sensor.py`: only for optional disabled diagnostic entities.
- `strings.json` and `translations/en.json`: update together for new UI text.
- `README.md` and `CHANGELOG.md`: document automatic passive detection and both
  firmware images.

No config-entry migration or firmware selector is needed. Detection is runtime
state, so one integration release supports original, improved legacy, and V2
boards.

## Required tests

Add mocked serial tests for:

- unsolicited V2 startup detection without any host write;
- exact assertion that connection setup sends no probe bytes;
- original and improved legacy identification;
- a real legacy alarm event being processed while establishing mode;
- attaching after V2 startup and identifying from heartbeat/event;
- reconnect redetection and cancellation of pending futures;
- every V2 type, malformed input, wrong types, unknown fields/types, and
  late/duplicate IDs;
- exact legacy bytes and compact V2 JSON/LF bytes;
- command buttons unavailable while mode is unknown;
- concurrent V2 correlation with interleaved alarm events;
- pairing-time commands receiving a correlated `busy` error without UART loss;
- command acceptance not changing `last_test_pass`;
- original/improved legacy bridge-ID `PASS` being retained as a bridge action
  but ignored as a detector result;
- genuine received test `PASS` changing `last_test_pass`;
- separate physical-button events updating only their originating detector;
- unchanged discovery, persistence, partial updates, and legacy behavior.

Run the integration repository checks:

```text
ruff check .
ruff format --check .
pytest
```

## Rollout

1. Release HA support for passive legacy/V2 detection.
2. Verify original and improved legacy firmware before recommending reflashing.
3. Users who want protocol 2 flash the separate V2 image.
4. Confirm HA receives the V2 startup message and reports protocol 2.
5. Retain legacy support indefinitely unless removal is a separate announced
   decision.
