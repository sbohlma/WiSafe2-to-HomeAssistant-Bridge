#ifndef WISAFE_RADIO_CORE_H
#define WISAFE_RADIO_CORE_H

#include <Arduino.h>
#include <SPI.h>
#include <avr/wdt.h>

#define IRQline 9
#define directModePin 2

const byte SPI_BUFFER_SIZE = 25;

extern volatile boolean SPIreceived;
extern volatile byte Slavereceived;
extern byte spiBuffer[SPI_BUFFER_SIZE];
extern byte spiBufferIndex;
extern boolean radioReceiveBufferReady;
extern boolean spiFrameOverflow;
extern unsigned int spiOverflowCount;
extern unsigned int malformedFrameCount;
extern unsigned int incompleteFrameTimeoutCount;
extern unsigned int commandTimeoutCount;
extern unsigned int commandRetryCount;
extern unsigned int unknownFrameCount;
extern unsigned int radioReinitializationCount;
extern byte lastUnknownFrameType;
extern unsigned long spiFrameStartedAt;
extern boolean directMode;
extern boolean radioReady;
extern int maxInitAttempts;
extern unsigned int heartBeatInterval;
extern unsigned long lastHeartBeat;

enum RadioEventKind : byte {
  RADIO_EVENT_UNKNOWN,
  RADIO_EVENT_TEST,
  RADIO_EVENT_STATUS,
  RADIO_EVENT_EMERGENCY,
  RADIO_EVENT_SILENCE,
  RADIO_EVENT_MISSING
};

enum AlarmKind : byte {
  ALARM_UNSPECIFIED,
  ALARM_FIRE,
  ALARM_CARBON_MONOXIDE
};

struct RadioEvent {
  RadioEventKind kind;
  AlarmKind alarm;
  byte device[3];
  byte model[2];
  byte rawMessageType;
  byte rawEventCode;
  byte rawStatus;
  boolean testPassed;
  boolean baseOn;
  boolean batteryLow;
};

void flushSPIbuffer();
void checkIfSPIbufferIsStagnant();
boolean responseIs(byte firstByte, byte expectedLength);
void ReadByteFromRadio();
void WriteByteToRadio(byte byteToSend);
void serviceRadioFrame();
void cooperativeWait(unsigned long durationMs);
void writeCommand(const byte cmd[], byte cmdSize);
boolean sendAndExpect(const byte cmd[], byte cmdSize, byte responseByte,
                      byte responseLength, byte maxAttempts);
boolean requireFrameLength(byte minimumLength);
AlarmKind decodeAlarm(byte value);
boolean decodeRadioEvent(RadioEvent &event);
void printHexByte(byte value);
void printDevice(const byte device[3]);

#endif
