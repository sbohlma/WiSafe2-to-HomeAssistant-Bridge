#include "WiSafeRadioCore.h"

// Protocol-specific callbacks implemented by each sketch.
extern void processRadioResponse();
extern void processHeartBeat();
extern void serviceSerialInputDuringWait();

volatile boolean SPIreceived;
volatile byte Slavereceived;
int IRQState = 1;
int IRQLastState = 1;
int sendWait = 0;
byte spiBuffer[SPI_BUFFER_SIZE];
byte spiBufferIndex = 0;
boolean radioReceiveBufferReady = false;
boolean spiFrameOverflow = false;
unsigned int spiOverflowCount = 0;
unsigned int malformedFrameCount = 0;
unsigned int incompleteFrameTimeoutCount = 0;
unsigned int commandTimeoutCount = 0;
unsigned int commandRetryCount = 0;
unsigned int unknownFrameCount = 0;
unsigned int radioReinitializationCount = 0;
byte lastUnknownFrameType = 0;
unsigned long spiFrameStartedAt = 0;
boolean directMode = false;
boolean radioReady = false;
int maxInitAttempts = 50;
unsigned int heartBeatInterval = 25000;
unsigned long lastHeartBeat = 0;
const unsigned long SPI_FRAME_TIMEOUT_MS = 100;
const unsigned long COMMAND_RESPONSE_TIMEOUT_MS = 150;

ISR (SPI_STC_vect)
{
  Slavereceived = SPDR;
  SPIreceived = true;
}

void flushSPIbuffer()
{
  spiBufferIndex = 0;
  memset(spiBuffer, 0, sizeof(spiBuffer));
  radioReceiveBufferReady = false;
  spiFrameOverflow = false;
}

void checkIfSPIbufferIsStagnant()
{
  if ((spiBufferIndex > 0 || spiFrameOverflow) &&
      (millis() - spiFrameStartedAt >= SPI_FRAME_TIMEOUT_MS)) {
    incompleteFrameTimeoutCount++;
    flushSPIbuffer();
  }
}

boolean responseIs(byte firstByte, byte expectedLength)
{
  return (radioReceiveBufferReady && spiBufferIndex == expectedLength &&
          spiBuffer[0] == firstByte && spiBuffer[expectedLength - 1] == 0x7E);
}

void ReadByteFromRadio()
{
  IRQState = digitalRead(SS);
  if (IRQState != IRQLastState)
  {
    if (IRQState == HIGH)
    {
      digitalWrite(IRQline, HIGH);
      delayMicroseconds(8);
      digitalWrite(IRQline, LOW);
    }
    IRQLastState = IRQState;
    if (SPIreceived == true)
    {
      if (spiFrameOverflow) {
        if (Slavereceived == 0x7E) flushSPIbuffer();
      } else if (spiBufferIndex < SPI_BUFFER_SIZE) {
        if (spiBufferIndex == 0) spiFrameStartedAt = millis();
        spiBuffer[spiBufferIndex++] = Slavereceived;
        if (Slavereceived == 0x7E) radioReceiveBufferReady = true;
      } else {
        spiOverflowCount++;
        spiFrameOverflow = true;
        spiFrameStartedAt = millis();
        spiBufferIndex = 0;
        radioReceiveBufferReady = false;
      }
    }
    (SPDR = 0);
    (SPIreceived = false);
  }
}

void WriteByteToRadio(byte byteToSend)
{
  digitalWrite(IRQline, HIGH);                    //set IRQ high
  sendWait = 0;
  while (digitalRead(SS) == HIGH)
  {
    delayMicroseconds(5);                         //wait for CS to go low
    sendWait++;
    if (sendWait >= 1000)
    { //timeout waiting for CS
      break;
    }
  }
  sendWait = 0;
  SPDR = byteToSend;                              //put data from Slave to Master on to SPI
  sendWait = 0;
  while (digitalRead(SS) == LOW)
  {
    delayMicroseconds(5);                         //wait for CS to return high
    sendWait++;
    if (sendWait >= 1000)
    { //timeout waiting for CS
      break;
    }
  }
  digitalWrite(IRQline, LOW);                     //set IRQ low
  (SPDR = 0);
  (SPIreceived = false);
}

void serviceRadioFrame()
{
  ReadByteFromRadio();
  checkIfSPIbufferIsStagnant();
  if (!radioReceiveBufferReady) return;
  if (directMode) Serial.write(spiBuffer, spiBufferIndex);
  else processRadioResponse();
  flushSPIbuffer();
}

void cooperativeWait(unsigned long durationMs)
{
  unsigned long startedAt = millis();
  while (millis() - startedAt < durationMs) {
    serviceRadioFrame();
    processHeartBeat();
    serviceSerialInputDuringWait();
    wdt_reset();
  }
}

void writeCommand(const byte cmd[], byte cmdSize)
{
  for (byte i = 0; i < cmdSize; i++) WriteByteToRadio(cmd[i]);
}

boolean sendAndExpect(const byte cmd[], byte cmdSize, byte responseByte,
                      byte responseLength, byte maxAttempts)
{
  for (byte attempt = 0; attempt < maxAttempts; attempt++) {
    flushSPIbuffer();
    writeCommand(cmd, cmdSize);
    unsigned long startedAt = millis();
    while (millis() - startedAt < COMMAND_RESPONSE_TIMEOUT_MS) {
      ReadByteFromRadio();
      checkIfSPIbufferIsStagnant();
      if (radioReceiveBufferReady) {
        if (responseIs(responseByte, responseLength)) return true;
        if (directMode) Serial.write(spiBuffer, spiBufferIndex);
        else processRadioResponse();
        flushSPIbuffer();
      }
      wdt_reset();
    }
    commandTimeoutCount++;
    if (attempt + 1 < maxAttempts) {
      commandRetryCount++;
      cooperativeWait(500);
    }
  }
  return false;
}

boolean requireFrameLength(byte minimumLength)
{
  if (spiBufferIndex >= minimumLength) return true;
  malformedFrameCount++;
  return false;
}

AlarmKind decodeAlarm(byte value)
{
  if (value == 0x41) return ALARM_CARBON_MONOXIDE;
  // Captures establish both as fire-family values, but do not establish a
  // defensible smoke-versus-heat distinction.
  if (value == 0x81 || value == 0x82) return ALARM_FIRE;
  return ALARM_UNSPECIFIED;
}

boolean decodeRadioEvent(RadioEvent &event)
{
  memset(&event, 0, sizeof(event));
  if (!radioReceiveBufferReady || spiBufferIndex < 2 || spiBuffer[spiBufferIndex - 1] != 0x7E) {
    malformedFrameCount++;
    return false;
  }
  event.rawMessageType = spiBuffer[0];
  switch (event.rawMessageType) {
    case 0x70:
      // Received from a detector when its physical test button is pressed.
      if (!requireFrameLength(9)) return false;
      event.kind = RADIO_EVENT_TEST;
      memcpy(event.device, &spiBuffer[1], 3);
      memcpy(event.model, &spiBuffer[6], 2);
      event.rawEventCode = spiBuffer[4];
      event.rawStatus = spiBuffer[5];
      event.alarm = decodeAlarm(event.rawEventCode);
      event.testPassed = event.rawStatus == 0x01;
      event.baseOn = true;
      event.batteryLow = false;
      return true;
    case 0x71:
      if (!requireFrameLength(8)) return false;
      event.kind = RADIO_EVENT_STATUS;
      memcpy(event.device, &spiBuffer[1], 3);
      memcpy(event.model, &spiBuffer[4], 2);
      event.rawStatus = spiBuffer[6];
      event.baseOn = event.rawStatus & 0x04;
      event.batteryLow = event.rawStatus & 0x42;
      return true;
    case 0x50:
      if (!requireFrameLength(7)) return false;
      event.kind = RADIO_EVENT_EMERGENCY;
      memcpy(event.device, &spiBuffer[1], 3);
      event.rawEventCode = spiBuffer[4];
      event.rawStatus = spiBuffer[5];
      event.alarm = decodeAlarm(event.rawEventCode);
      event.baseOn = true;
      return true;
    case 0x61:
      if (!requireFrameLength(7)) return false;
      event.kind = RADIO_EVENT_SILENCE;
      memcpy(event.device, &spiBuffer[1], 3);
      event.rawEventCode = spiBuffer[4];
      event.rawStatus = spiBuffer[5];
      event.baseOn = true;
      return true;
    case 0xD2:
      if (!requireFrameLength(10)) return false;
      event.kind = RADIO_EVENT_MISSING;
      memcpy(event.device, &spiBuffer[6], 3);
      return true;
    default:
      lastUnknownFrameType = event.rawMessageType;
      unknownFrameCount++;
      return false;
  }
}

void printHexByte(byte value)
{
  if (value <= 0x0F) Serial.print('0');
  Serial.print(value, HEX);
}

void printDevice(const byte device[3])
{
  printHexByte(device[0]);
  printHexByte(device[1]);
  printHexByte(device[2]);
}
