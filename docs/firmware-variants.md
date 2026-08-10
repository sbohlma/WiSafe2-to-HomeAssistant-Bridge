# Firmware variants

The repository contains two independent Arduino Nano sketches. Flash exactly
one; protocol selection is not a runtime setting.

## Improved legacy firmware

Path: `Arduino/FireAngelNano.ino`

This is based on the Stage 3 firmware: memory-safe reception, responsive management
operations, watchdog recovery, internal protocol modelling, and diagnostics,
while preserving the original serial framing and commands. One misleading
legacy behavior is deliberately corrected: a bridge-originated sound command
no longer creates a synthetic detector `PASS` event.

Choose it when using the original Home Assistant serial configuration or an
integration version that only understands the C19HOP protocol. It retains:

- legacy command bytes and `0x7E` input framing;
- legacy JSON event fields and strings;
- cycling legacy heartbeat;
- textual command/pairing responses;
- `CMD OK`/`CMD FAIL` reporting for bridge-originated sound commands, without a
  synthetic detector result;
- direct/raw jumper mode.

## V2-only firmware

Path: `Arduino-V2/FireAngelNanoV2.ino`

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

## Detector testing semantics

The attached donor radio can transmit fire, CO, or combined interlink test
signals. Other alarms sound when they receive the signal, but they do not send
a response or test result to the bridge. `CMD OK` in legacy firmware and
`command_result: accepted` in V2 confirm only that the attached radio accepted
the transmission process; they do not confirm reception, detector operation,
or a passed test.

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

## Arduino CLI builds

The Arduino CLI requires the sketch directory and main `.ino` basename to
match. The repository paths already satisfy that rule for the V2 sketch. For
the legacy sketch, the historic directory/file names differ, so copy it into a
temporary matching directory before compiling.

Example targets:

```text
arduino:avr:nano:cpu=atmega328old
```

Select `ATmega328P (Old Bootloader)` in the Arduino IDE for typical Nano clones.
