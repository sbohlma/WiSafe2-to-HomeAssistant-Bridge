# Firmware variants

The repository contains two independent Arduino Nano sketches for Home
Assistant and one test firmware. Flash exactly one; protocol selection is not a
runtime setting.

## Improved legacy firmware

Path: `Arduino-Firmware/FireAngelNanoLegacy/FireAngelNanoLegacy.ino`

This is based on the Stage 3 firmware: memory-safe reception, responsive management
operations, watchdog recovery, internal protocol modelling, and diagnostics,
while preserving the original serial framing, commands, and bridge-device
initiation record.

Choose it when using the original Home Assistant serial configuration or an
integration version that only understands the C19HOP protocol. It retains:

- legacy command bytes and `0x7E` input framing;
- legacy JSON event fields and strings;
- cycling legacy heartbeat;
- textual command/pairing responses;
- `CMD OK`/`CMD FAIL` reporting and the historical synthetic bridge-device
  `PASS` after a bridge-originated sound command;
- direct/raw jumper mode.

## V2-only firmware

Path: `Arduino-Firmware/FireAngelNanoV2/FireAngelNanoV2.ino`

Choose it only with a Home Assistant integration that supports protocol 2. In
normal mode it emits newline-delimited JSON exclusively and identifies itself
after every successful boot:

```json
{"type":"bridge","event":"startup","firmware":"2.0.0","protocol":2,"radio":"ready"}
```

It does not accept legacy commands, does not negotiate, and cannot switch to
legacy mode. Command acceptance and genuine detector test results are separate.
The direct/raw hardware jumper remains available and retains raw `0x7E`
framing.

## Test firmware

Path: `Arduino-Firmware/FireAngelNanoTest/FireAngelNanoTest.ino`

A serial console to the radio module for protocol experiments; Home Assistant
does not understand it. It builds on what was observed on a real network
(FireAngel `wisafe-mm/docs/PROTOCOL_ANALYSIS_W2_MODULE.md`):

- Nothing is sent at startup. The other images send `D3 19 50 00` at every
  reset, which the radio module floods through the whole network, probably for
  the reason under [Hardware: 1 kΩ pull-down on IRQ](#hardware-1-kω-pull-down-on-irq-d9);
  opening the serial port resets a Nano.
- Frames to the radio module are escaped (`7E` as `7D 01`, `7D` as `7D 02`).
  Received frames are unescaped, `00` filler before a frame is dropped.
- After an init and while remote requests are open, the radio module does not
  clock bytes from the host for up to about 20 s. A frame of which no byte went
  out is repeated every 500 ms for up to 60 s.
- Commands keep 500 ms of quiet after the previous frame. Answers to the radio
  module go first and do not wait.
- Every `41` is answered with `91 dev(3) model(2) class 05 01 01`. `C3` and
  `C1` are answered with `C4` and `C2`, and `72`, `C7` and `C5` with `73`,
  `C8` (3000 mV) and `C6` if they carry the own device ID; each request type at
  most once per 60 s.
- Copies of network messages (same SID, type and sequence number within 60 s)
  are marked and not answered again.
- Bytes from the radio module are acknowledged in a pin-change interrupt on
  chip select, so serial output cannot delay the IRQ pulse.

Serial: 115200 baud, one command per line, case-insensitive.

| Command | Frame | Note |
|---|---|---|
| `STATUS` | `D1` | status of the own radio module (`D2`) |
| `MAP` | `D3 03` | local SID map (`D4 03`) |
| `RS <sid>` | `D3 06 sid 00` | remote status of a radio module (`D4 06`) |
| `IDQ <sid>` | `D3 06 sid 01` | ID request to the host of a radio module (`C4`) |
| `S <hex>` | as given | read-only requests only: `D1`, `D3 03`, `D3 06 sid 00/01`, `72`/`C5`/`C7` with 7 bytes |
| `S! <hex>` | as given | any frame up to 16 bytes; can sound detectors or change the network |
| `INIT!` | `D3 19 50 00` | flooded through the network |
| `IDENT [<id> <model> <class>]` | – | show or set the identity, e.g. `IDENT 13B8A5 785 FF`; stored in EEPROM, the radio module takes it at its next `41` |
| `AUTO ON\|OFF` | – | automatic answers; stored in EEPROM |
| `INFO`, `HELP` | – | settings and counters, command list |

SIDs are decimal, device IDs in display order (`13B8A5` is sent as
`A5 B8 13`), models decimal (785 = WST-630), classes hex. The default identity
is that of the other images: `13B8A5`, model 785, class `FF`.

Output lines start with a tag and the `millis()` time:

```text
I FireAngelNanoTest 1.0.0 ready; nothing is sent at startup, HELP lists the commands
TX 5012 D3 06 1A 00: ok (46)
RX 9310 D4 06 1A 64 8C 8C 7D EF 3D E4 2F 00 00  remote status SID 26, RM battery 2716 mV, ...
RX 12001 C3 00 00 00 0F 0B  ID request, SID 15, seq 11
AUTO 12020 C4 A5 B8 13 FF 11 03 (to C3): ok (46)
```

`RX` is a frame from the radio module (unescaped), `TX` an own command with its
result, `AUTO` an automatic answer, `I`, `W`, `E` information, warning and error.
The `46`/`47` that completes a command appears in its `TX` line only.

The protocol logic without Arduino dependencies is in `w2_protocol.cpp`; the
host tests in `tests/` compile with any C++11 compiler:

```text
c++ -std=c++11 -I.. ../w2_protocol.cpp test_w2_protocol.cpp -o test_w2_protocol
```

### Hardware: 1 kΩ pull-down on IRQ (D9)

Fit a 1 kΩ resistor from D9 to GND on the Nano side of the level converter.
Without it, the first frame after every reset of the Nano reaches the network.

Observed on 26 Oct 2026 with the Nano at SID 16 (PCB with BSS138 level
converters, radio module powered by the Nano), a logic analyser on the radio
module side of the level converters and a W2-MM dongle listening:

- Every reset of the Nano, including opening the serial port, left bytes in
  the radio module. The radio module clocked one full byte at the moment of the
  reset and a cut-off one (3 clocks) about 1.1 s later, inside the bootloader;
  nothing else happened on the bus until the first command.
- The radio module then took the next frame from the host as a radio message.
  `D1` was acknowledged with `46` instead of answered with `D2`, and every node
  relayed a type `00` message `00 00 D1` from the own SID once, for about 7 s,
  without any alarm. The second frame worked normally.
- The radio module does not restart at a reset: it clocks normally, keeps its
  SID and answers the second `D1` without an init.
- During a reset D9 floats, and the 10 kΩ pull-up of the level converter pulls
  it up. The edge makes the radio module clock a byte. A 10 kΩ pull-down only
  halves the edge to about 2.5 V; it changed the relayed message (no payload
  any more) but did not prevent it.
- With 1 kΩ, D9 stays at about 0.45 V. On 27 Oct 2026 three resets in a row
  left no byte on the bus, the first `D1` was answered with `D2` each time, and
  the dongle heard no radio message. D9 then sources about 5 mA while high.

Still open: on the radio module side IRQ is high for only about 1 µs per pulse,
although the Nano holds it high for a whole byte. The radio module reacts to
the edge and works reliably; why the level does not stay high is not known.

Without the pull-down, make `STATUS` the first command after every reset and
keep `AUTO OFF`, so that an automatic answer is never the first frame.

## Detector testing semantics

The attached donor radio can transmit fire, CO, or combined interlink test
signals. Other alarms sound when they receive the signal, but they do not send
a response or test result to the bridge. `CMD OK` in legacy firmware and
`command_result: accepted` in V2 confirm only that the attached radio accepted
the transmission process; they do not confirm reception, detector operation,
or a passed test.

The legacy firmware additionally emits a synthetic `PASS` event using the
configured bridge device ID. This is the legacy representation of the bridge
having initiated the signal—the equivalent of V2's `command_result: accepted`.
It is not a reply from any detector and must not update a detector's physical
test history.

Pressing the physical test button on a detector is different. That detector
transmits its own test event across the WiSafe2 network, including to the
bridge. The firmware records this received frame against the originating
detector's device ID. To test and record every detector, press every detector's
button separately. There is no known remote command that individually tests all
detectors or gathers their results.

## Shared hardware and RF behavior

Both target the classic-bootloader Arduino Nano / ATmega328P at 5 V and 16 MHz.
Both retain the existing pins, SPI-slave role, 115200 baud serial link, embedded
bridge identity, genuine donor radio, RF templates, and supported alarm events.
No PCB or wiring change is required when moving between images.

The byte acquisition, bounded transaction, watchdog-safe wait, diagnostics, and
frame decoder are shared by both sketches through the local
`libraries/WiSafeRadioCore` Arduino library. Radio/SPI fixes belong there rather
than being copied into each protocol frontend.

The test firmware runs on the same hardware and pins but does not use
`WiSafeRadioCore`: it brings its own interrupt-driven SPI handling and frame
decoder, needs no extra library, and does not enable the watchdog. It needs
the 1 kΩ pull-down on D9 described under the test firmware; the other images
probably benefit from it too.

## Arduino CLI builds

Install or copy `libraries/WiSafeRadioCore` into the Arduino sketchbook's
`libraries` directory when building in the Arduino IDE. With Arduino CLI, pass
the repository library directory explicitly:

```text
--libraries /path/to/WiSafe2-to-HomeAssistant-Bridge/libraries
```

The Arduino IDE and CLI require the sketch directory and main `.ino` basename
to match. All sketches under `Arduino-Firmware/` follow that rule, so they can
be opened and compiled in place.

Example targets:

```text
arduino:avr:nano:cpu=atmega328old
```

Select `ATmega328P (Old Bootloader)` in the Arduino IDE for typical Nano clones.
