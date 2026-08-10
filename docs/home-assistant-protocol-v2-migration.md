# Home Assistant protocol v2 migration

This is the implementation plan for
[`NikNakk/fireangel-pro-connected-component`](https://github.com/NikNakk/fireangel-pro-connected-component).
It is based on repository commit `46bf7b2` (2026-08-10). No changes to that
repository are included here.

The migration target must support all three combinations during rollout:

| Firmware | Transport selected by HA | Required result |
| --- | --- | --- |
| Original C19HOP | Legacy | Fully supported |
| Fork firmware 2.x | Legacy | Fully supported |
| Fork firmware 2.x | Protocol v2 | Preferred, fully supported |

The wire format is defined in [serial-protocol-v2.md](serial-protocol-v2.md).

## Recommended architecture

Keep serial ownership and protocol dispatch in
`custom_components/fireangel_pro_connected/bridge.py`. Add a small protocol
state enum (`NEGOTIATING`, `LEGACY`, `V2`) and keep detector/entity code
independent of the wire version. Decode both protocols into the existing
`DetectorState` update path.

Do not create a second serial reader. The existing `readline()` loop remains a
good fit because legacy JSON and v2 JSON are both LF-terminated. All writes and
request-ID allocation should be protected by one `asyncio.Lock`, while pending
v2 command futures are held in a dictionary keyed by ID.

Suggested bridge runtime fields:

- `protocol_version: int | None` (`None` means legacy/unknown)
- `firmware_version: str | None`
- `radio_state: str | None`
- `bridge_uptime: int | None`
- `diagnostic_counters: dict[str, int]`
- `last_error: str | None`
- `last_command_result: dict[str, Any] | None`
- `_next_request_id: int`
- `_pending_commands: dict[int, asyncio.Future[dict[str, Any]]]`
- `_write_lock: asyncio.Lock`

## Negotiation and reconnects

Immediately after opening the serial port in `_async_connect()`:

1. Set state to `NEGOTIATING`.
2. Write exactly `b"V2~"` and drain the writer.
3. Read for a short bounded window (recommended: one second).
4. If a `{"type":"bridge","event":"ready","protocol":2,...}` line arrives,
   select v2 and store firmware/radio metadata.
5. If no valid ready response arrives, select legacy and continue normally.

The original C19HOP firmware safely ignores the negotiation because `V` is not
a recognised legacy command. Do not require users to choose a firmware version
in config flow.

Any valid legacy event received during the negotiation window must be processed
immediately, not dropped while waiting for the ready response. A legacy
`heartBeat` record is sufficient evidence to select legacy early. Malformed or
plain-text lines should update the existing last-message diagnostic but must
not abort negotiation or the reader.

Run this handshake after every serial reconnect because the Arduino may have
reset. Cancel all pending command futures with a connection-lost exception
before reconnecting. If a legacy heartbeat is observed while the integration
believes it is in v2, treat that as a firmware reset/protocol downgrade and
renegotiate safely from the reader task.

## Message dispatch

Change `async_process_line()` to dispatch on `payload.get("type")` when it is a
string. If `type` is absent, use the existing legacy parser unchanged. Suggested
v2 handlers are:

- `bridge`: validate `protocol == 2`; update version and radio fields.
- `heartbeat`: update `last_heartbeat`, bridge uptime, and radio state.
- `status`: update version, protocol, uptime, radio state, and validated
  diagnostic counters.
- `event`: pass only this message type into detector discovery/state updates.
- `command_result`: resolve the matching pending request and update command
  diagnostics; never update a detector.
- `error`: resolve/fail a matching request when `id` exists, record the error,
  and continue reading.
- Unknown types: retain as `last_message`, log at debug level, and ignore.

Validate types and ranges defensively. A malformed message, unknown event,
unknown field, late command result, or duplicate result must never terminate
the serial reader. Ignore unknown fields for forward compatibility.

## Event normalization

To minimise entity and automation churn, translate v2 event names at the
protocol boundary into the integration's current internal strings:

| V2 event | Existing internal value |
| --- | --- |
| `FIRE_TEST` | `FIRE TEST` |
| `CO_TEST` | `CARBON MONOXIDE TEST` |
| `TEST` | `TEST` |
| `FIRE_EMERGENCY` | `FIRE EMERGENCY` |
| `CO_EMERGENCY` | `CARBON MONOXIDE EMERGENCY` |
| `EMERGENCY` | `EMERGENCY` |
| `STATUS` | Do not replace the detector's last event; update base/battery only |
| `SILENCE` | `SILENCE` |
| `MISSING` | `MISSING` |

Continue uppercase device/model normalization and partial-update merging.
Preserve `raw_status` on `DetectorState` as optional diagnostic data, but do not
infer new battery, base, heat, or smoke semantics from unknown bits.

Only a v2 message with `type == "event"`, a test event name, and
`result == "PASS"` may update `last_test_pass`. A `command_result` whose result
is `accepted` means only that the bridge transmitted/accepted the request. It
must not update detector state or the last-successful-test timestamp.

Continue the current conservative smoke/heat handling. Protocol v2 deliberately
does not distinguish WiSafe2 values `0x81` and `0x82`.

## Sending commands and correlation

Replace byte-valued button descriptions with semantic command keys, or retain a
mapping containing both encodings:

| Semantic command | Legacy bytes | V2 command |
| --- | --- | --- |
| CO test | `b"1~"` | `co_test` |
| Fire test | `b"2~"` | `fire_test` |
| Combined test | `b"3~"` | `combined_test` |
| Silence as CO | `b"6~"` | `silence_co` |
| Silence as fire | `b"7~"` | `silence_fire` |
| Pairing state | `b"8~"` | `pairing_state` |
| Start pairing | `b"9~"` | `pairing` |

Keep emergency simulation absent from buttons, services, and the v2 map.

In v2, allocate an unused unsigned 16-bit ID, encode compact JSON with
`separators=(",", ":")`, append `b"\n"`, register the response future before
writing, then drain. Resolve it on the first matching `command_result` or
`error`. Use a bounded HA-side timeout and remove the future in `finally`.
Unsolicited events continue through normal dispatch while the future is pending.

For legacy firmware, retain today's exact bytes and fire-and-forget behaviour.
The public `async_send_command()` API can return a structured result in v2 and
`None` in legacy so button behavior remains compatible while future UI work can
surface failures.

Pairing emits `accepted` first and a later `paired`/`unpaired` result with the
same ID. Treat `accepted` as completion for a button press, but retain the later
result as bridge diagnostic state. `pairing_state` completes on
`paired`/`unpaired`.

## Heartbeat and availability

Treat both legacy `heartBeat` and v2 `type: heartbeat` as activity. Preserve the
existing serial connection binary sensor. If heartbeat age is later used for
availability, allow comfortably more than the firmware's 25-second interval
and do not confuse a quiet alarm network with a disconnected serial port.

Store v2 `uptime` as the latest raw 32-bit millisecond value. A lower subsequent
value can indicate reset or `millis()` rollover; it is not itself an error.

## Diagnostics and entities

Extend `diagnostics.py` with protocol version, firmware version, radio state,
last heartbeat, last error, last command result, and firmware diagnostic
counters. Do not include raw serial-port contents beyond the existing user
visible last-message behavior, and continue normal HA redaction practices.

Optionally expose firmware version and radio state as disabled-by-default
diagnostic sensors in `sensor.py`. Avoid creating seven counter entities by
default; the HA diagnostics download is a better home unless users demonstrate
an operational need.

## File-level change list

- `bridge.py`: negotiation, protocol state, typed v2 dispatch, correlation IDs,
  pending futures, semantic command encoder, bridge metadata, and robust error
  handling.
- `const.py`: protocol constants, semantic command names, legacy/v2 mapping,
  negotiation bytes, and timeouts.
- `button.py`: send semantic commands rather than fixed bytes; keep the same
  entities and unique IDs.
- `diagnostics.py`: include negotiated metadata and counters.
- `sensor.py`: only if adding optional firmware/radio diagnostic entities.
- `strings.json` and `translations/en.json`: update together if new entities or
  user-visible errors are added.
- `README.md` and integration `CHANGELOG.md`: compatibility matrix, automatic
  negotiation, firmware upgrade behavior, and command/test-result semantics.

No config-entry migration is required if protocol selection remains automatic
runtime state. Do not persist a forced protocol version: automatic negotiation
is what lets one integration build support all three firmware cases.

## Required tests

Add mocked-reader/writer tests for:

- successful v2 negotiation and exact `b"V2~"` bytes;
- one-second fallback to original firmware without setup failure;
- a legacy alarm event arriving during negotiation;
- renegotiation and pending-future cancellation after reconnect;
- v2 bridge, heartbeat, status, every event type, command result, and error;
- malformed JSON, non-object JSON, missing/wrong field types, unknown message
  types, oversized/late/duplicate IDs, and unknown enum values;
- compact v2 command bytes and monotonically allocated/wrapping IDs;
- concurrent commands correlated correctly despite interleaved events;
- command acceptance not changing `last_test_pass`;
- genuine received test `PASS` changing `last_test_pass`;
- unchanged legacy parsing, exact command bytes, discovery, persistence, and
  partial updates;
- original firmware, fork legacy mode, and fork v2 compatibility fixtures.

Run the integration repository's required checks after implementation:

```text
ruff check .
ruff format --check .
pytest
```

## Rollout order

1. Release the HA integration with negotiation plus all legacy fallbacks.
2. Verify it against original firmware before recommending any reflash.
3. Flash firmware 2.x; the updated integration should negotiate v2
   automatically.
4. Monitor error/counter diagnostics during an observation period.
5. Retain legacy support for existing users; removing it should be a separate,
   explicitly announced future decision, not part of this migration.
