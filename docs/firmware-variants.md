# Firmware variants

The repository contains two independent Arduino Nano sketches. Flash exactly
one; protocol selection is not a runtime setting.

## Improved legacy firmware

Path: `Arduino/FireAngelNano.ino`

This is the Stage 3 firmware: memory-safe reception, responsive management
operations, watchdog recovery, internal protocol modelling, and diagnostics,
while preserving the original observable serial protocol.

Choose it when using the original Home Assistant serial configuration or an
integration version that only understands the C19HOP protocol. It retains:

- legacy command bytes and `0x7E` input framing;
- legacy JSON event fields and strings;
- cycling legacy heartbeat;
- textual command/pairing responses;
- historical synthetic post-command test messages;
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
