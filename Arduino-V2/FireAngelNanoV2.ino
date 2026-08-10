#include<SPI.h>
#include <avr/wdt.h>
//#include <ArduinoJson.h>

#define IRQline 9
#define directModePin 2

volatile boolean SPIreceived;
volatile byte Slavereceived, Slavesend;
int IRQState = 1;
int IRQLastState = 1;
int sendWait = 0;
const byte SPI_BUFFER_SIZE = 25;
const byte SERIAL_BUFFER_SIZE = 79;
char serialBuffer[SERIAL_BUFFER_SIZE + 1];
byte spiBuffer[SPI_BUFFER_SIZE];
byte serialBufferIndex = 0;
byte spiBufferIndex = 0;
boolean serialFrameOverflow = false;
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
boolean activeCommandHasId = false;
unsigned int activeCommandId = 0;
int maxInitAttempts = 50;                        //MAX number of attempts to Init Radio
unsigned int heartBeatInterval = 25000;           // Heartbeat interval (in milliseconds)
unsigned long lastHeartBeat = 0;
const unsigned long SPI_FRAME_TIMEOUT_MS = 100;
const unsigned long COMMAND_RESPONSE_TIMEOUT_MS = 150;
const char FIRMWARE_VERSION[] = "2.0.0";
const byte SERIAL_PROTOCOL_VERSION = 2;

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


//------------------------EMBEDDED DEVICE INFO GOES HERE-------------------------------
// When sending data to other alarms, this is the DeviceID and ModelID which other
// devices on the network will receive data from

// Device ID
// Just needs to be unique
byte DevId0 = 0xA5;
byte DevId1 = 0xB8;
byte DevId2 = 0x13;

// Device Model
// Known devices are (ED:08==FP2620W2,  11:03==WST-630,  78:03==W2-CO-10X,  C3:04==W2-SVP-630)
byte DevModel0 = 0x11;
byte DevModel1 = 0x03;
//--------------------------------------------------------------------------------------



void(* resetFunc) (void) = 0;                                 //Reset Arduino if radio init failed



void setup()
{
  delay(5000);                                                 //Allow radio stabilise first
  pinMode(IRQline, OUTPUT);
  pinMode(directModePin, INPUT_PULLUP);
  pinMode(MISO, OUTPUT);                                       //Sets MISO as OUTPUT
  SPCR |= _BV(SPE);                                            //SPI = Slave Mode
  SPIreceived = false;
  SPI.attachInterrupt();                                       //Interrupt ON
  Serial.begin(115200);
  directMode = digitalRead(directModePin);
 // delay(5000);
  for (int i = 0; i <= maxInitAttempts; i++)
  {
    if (initRadio() == true) {
      radioReady = true;
      flushSPIbuffer();
      break;
    }
    radioReinitializationCount++;
    if (i == (maxInitAttempts)) {
      if (directMode == true) {
        Serial.write(0x15);
        Serial.write(0x7E);
      } else {
        Serial.println(F("{\"type\":\"error\",\"code\":\"radio_init_failed\"}"));
        delay(500);
      }
        resetFunc();
    }
  }
  wdt_enable(WDTO_2S);
  if (!directMode) printV2BridgeReady();
}



ISR (SPI_STC_vect)
{
  Slavereceived = SPDR;
  SPIreceived = true;
}



boolean initRadio()
{

  // I don't have enough information to know if init(1) or init(2) is best.
  byte cmdTemplate[] = {0xD3, 0x19, 0x50, 0x00, 0x7E}; // init(1)
  //byte cmdTemplate[] = {0xD3, 0x14, 0x8E, 0x7E}; // init(2)


  if (!sendAndExpect(cmdTemplate, sizeof(cmdTemplate), 0x46, 2, 1)) {
    return (false);
  }
  if (directMode == true) {
    Serial.write(0x06);
    Serial.write(0x7E);
  }
  return (true);
}



void checkIfSPIbufferIsStagnant()
{
  if ((spiBufferIndex > 0 || spiFrameOverflow) &&
      (millis() - spiFrameStartedAt >= SPI_FRAME_TIMEOUT_MS)) {
    incompleteFrameTimeoutCount++;
    flushSPIbuffer();
  }
}



void flushSPIbuffer()
{
  spiBufferIndex = 0;
  memset(spiBuffer, 0, sizeof(spiBuffer));
  radioReceiveBufferReady = false;
  spiFrameOverflow = false;
}


boolean responseIs(byte firstByte, byte expectedLength)
{
  return (radioReceiveBufferReady && spiBufferIndex == expectedLength &&
          spiBuffer[0] == firstByte && spiBuffer[expectedLength - 1] == 0x7E);
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


const __FlashStringHelper *commandName(byte command)
{
  switch (command) {
    case 0x31: return F("co_test");
    case 0x32: return F("fire_test");
    case 0x33: return F("combined_test");
    case 0x34: return F("co_emergency");
    case 0x35: return F("fire_emergency");
    case 0x36: return F("silence_co");
    case 0x37: return F("silence_fire");
    case 0x38: return F("pairing_state");
    case 0x39: return F("pairing");
    default: return F("unknown");
  }
}


void printV2Prefix(const __FlashStringHelper *type)
{
  Serial.print(F("{\"type\":\""));
  Serial.print(type);
  Serial.print('"');
}


void printCommandResult(byte command, const __FlashStringHelper *result)
{
  printV2Prefix(F("command_result"));
  if (activeCommandHasId) { Serial.print(F(",\"id\":")); Serial.print(activeCommandId); }
  Serial.print(F(",\"command\":\"")); Serial.print(commandName(command));
  Serial.print(F("\",\"result\":\"")); Serial.print(result);
  Serial.println(F("\"}"));
}


void printV2Error(const __FlashStringHelper *code)
{
  printV2Prefix(F("error"));
  if (activeCommandHasId) { Serial.print(F(",\"id\":")); Serial.print(activeCommandId); }
  Serial.print(F(",\"code\":\"")); Serial.print(code); Serial.println(F("\"}"));
}


void printV2BridgeReady()
{
  printV2Prefix(F("bridge"));
  Serial.print(F(",\"event\":\"startup\",\"firmware\":\"")); Serial.print(FIRMWARE_VERSION);
  Serial.print(F("\",\"protocol\":")); Serial.print(SERIAL_PROTOCOL_VERSION);
  Serial.print(F(",\"radio\":\"")); Serial.print(radioReady ? F("ready") : F("not_ready"));
  Serial.println(F("\"}"));
}


void printV2Status()
{
  printV2Prefix(F("status"));
  if (activeCommandHasId) { Serial.print(F(",\"id\":")); Serial.print(activeCommandId); }
  Serial.print(F(",\"firmware\":\"")); Serial.print(FIRMWARE_VERSION);
  Serial.print(F("\",\"protocol\":")); Serial.print(SERIAL_PROTOCOL_VERSION);
  Serial.print(F(",\"uptime\":")); Serial.print(millis());
  Serial.print(F(",\"radio\":\"")); Serial.print(radioReady ? F("ready") : F("not_ready"));
  Serial.print(F("\",\"diagnostics\":{\"overflow\":")); Serial.print(spiOverflowCount);
  Serial.print(F(",\"malformed\":")); Serial.print(malformedFrameCount);
  Serial.print(F(",\"incomplete\":")); Serial.print(incompleteFrameTimeoutCount);
  Serial.print(F(",\"unknown\":")); Serial.print(unknownFrameCount);
  Serial.print(F(",\"command_timeout\":")); Serial.print(commandTimeoutCount);
  Serial.print(F(",\"command_retry\":")); Serial.print(commandRetryCount);
  Serial.print(F(",\"radio_reinit\":")); Serial.print(radioReinitializationCount);
  Serial.println(F("}}"));
}


void SendTemplateToRadio(byte CMD)
{
  lastHeartBeat = millis();
  byte eventCode = 0;
  byte commandType = 0;
  byte attempts = 6;

  if (CMD >= 0x31 && CMD <= 0x33) {
    eventCode = (CMD == 0x31) ? 0x41 : ((CMD == 0x32) ? 0x81 : 0xFF);
    byte announce[] = {0x70, DevId0, DevId1, DevId2, eventCode, 0x01, DevModel0, DevModel1, 0x7E};
    attempts = 4;
    if (!sendAndExpect(announce, sizeof(announce), 0x41, 2, attempts)) {
      printCommandResult(CMD, F("timeout"));
      return;
    }
    byte transmit[] = {0x91, DevId0, DevId1, DevId2, DevModel0, DevModel1, eventCode, 0x05, 0x00, 0x02, 0x7E};
    flushSPIbuffer();
    writeCommand(transmit, sizeof(transmit));
    printCommandResult(CMD, F("accepted"));
    return;
  }

  if (CMD >= 0x34 && CMD <= 0x37) {
    commandType = (CMD <= 0x35) ? 0x50 : 0x61;
    eventCode = (CMD == 0x34) ? 0x41 : ((CMD == 0x35) ? 0x81 : ((CMD == 0x36) ? 0x40 : 0x80));
    byte command[] = {commandType, DevId0, DevId1, DevId2, eventCode, (byte)(commandType == 0x61), 0x7E};
    if (!sendAndExpect(command, sizeof(command), 0x46, 2, attempts)) {
      printCommandResult(CMD, F("timeout"));
      return;
    }
    printCommandResult(CMD, F("accepted"));
    return;
  }

  byte pairingState[] = {0xD3, 0x03, 0x7E};
  if (CMD == 0x38) {
    if (!sendAndExpect(pairingState, sizeof(pairingState), 0xD4, 11, 6)) {
      printCommandResult(CMD, F("timeout"));
      return;
    }
    printCommandResult(CMD, spiBuffer[2] ? F("paired") : F("unpaired"));
    return;
  }

  if (CMD == 0x39) {
    if (!sendAndExpect(pairingState, sizeof(pairingState), 0xD4, 11, 4)) {
      printCommandResult(CMD, F("timeout"));
      return;
    }
    if (spiBuffer[2]) {
      printCommandResult(CMD, F("already_paired"));
      return;
    }
    byte enablePairing[] = {0xD3, 0x12, 0x01, 0x7E};
    if (!sendAndExpect(enablePairing, sizeof(enablePairing), 0x46, 2, 4) ||
        !sendAndExpect(NULL, 0, 0x41, 2, 1)) {
      printCommandResult(CMD, F("timeout"));
      return;
    }
    byte pairingBroadcast[] = {0x91, DevId0, DevId1, DevId2, DevModel0, DevModel1, 0xFF, 0x05, 0x01, 0x01, 0x7E};
    flushSPIbuffer();
    writeCommand(pairingBroadcast, sizeof(pairingBroadcast));
    printCommandResult(CMD, F("accepted"));
    for (byte i = 0; i <= 20; i++) cooperativeWait(1000);
    if (!sendAndExpect(pairingState, sizeof(pairingState), 0xD4, 11, 1)) {
      printV2Error(F("pairing_state_timeout"));
      return;
    }
    printCommandResult(CMD, spiBuffer[2] ? F("paired") : F("unpaired"));
  }
}



boolean parseRequestId(const char *json, unsigned int &requestId)
{
  const char *position = strstr(json, "\"id\"");
  if (!position) return false;
  position = strchr(position, ':');
  if (!position) return false;
  position++;
  while (*position == ' ' || *position == '\t') position++;
  if (*position < '0' || *position > '9') return false;
  unsigned long value = 0;
  while (*position >= '0' && *position <= '9') {
    value = value * 10 + (*position++ - '0');
    if (value > 65535UL) return false;
  }
  requestId = (unsigned int)value;
  return true;
}


char *parseCommandName(char *json)
{
  char *position = strstr(json, "\"command\"");
  if (!position) return NULL;
  position = strchr(position, ':');
  if (!position) return NULL;
  position++;
  while (*position == ' ' || *position == '\t') position++;
  if (*position++ != '"') return NULL;
  char *end = strchr(position, '"');
  if (!end) return NULL;
  *end = '\0';
  return position;
}


void handleV2Command(char *json)
{
  boolean idPresent = strstr(json, "\"id\"") != NULL;
  activeCommandHasId = parseRequestId(json, activeCommandId);
  if (idPresent && !activeCommandHasId) { printV2Error(F("invalid_id")); return; }
  char *name = parseCommandName(json);
  if (!name) { printV2Error(F("malformed_command")); return; }
  byte command = 0;
  if (!strcmp(name, "co_test")) command = 0x31;
  else if (!strcmp(name, "fire_test")) command = 0x32;
  else if (!strcmp(name, "combined_test")) command = 0x33;
  else if (!strcmp(name, "silence_co")) command = 0x36;
  else if (!strcmp(name, "silence_fire")) command = 0x37;
  else if (!strcmp(name, "pairing_state")) command = 0x38;
  else if (!strcmp(name, "pairing")) command = 0x39;
  else if (!strcmp(name, "status")) { printV2Status(); return; }
  else {
    printV2Error(F("unknown_command"));
    return;
  }
  SendTemplateToRadio(command);
}


void completeSerialFrame()
{
  serialBuffer[serialBufferIndex] = '\0';
  if (directMode) {
    writeCommand((byte *)serialBuffer, serialBufferIndex);
    WriteByteToRadio(0x7E);
  } else {
    handleV2Command(serialBuffer);
  }
  serialBufferIndex = 0;
  serialFrameOverflow = false;
}


void ReadByteFromSerial()
{
  while (Serial.available() > 0) {
    byte received = Serial.read();
    boolean terminator = directMode ? (received == 0x7E) : (received == '\n');
    if (received == '\r') continue;
    if (terminator) {
      if (!serialFrameOverflow && serialBufferIndex > 0) completeSerialFrame();
      else {
        if (!directMode && serialFrameOverflow) {
          activeCommandHasId = false;
          printV2Error(F("serial_frame_overflow"));
        }
        serialBufferIndex = 0;
        serialFrameOverflow = false;
      }
    } else if (!serialFrameOverflow) {
      if (serialBufferIndex < SERIAL_BUFFER_SIZE) serialBuffer[serialBufferIndex++] = (char)received;
      else serialFrameOverflow = true;
    }
  }
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


void emitV2Event(const RadioEvent &event)
{
  printV2Prefix(F("event"));
  Serial.print(F(",\"device\":\"")); printDevice(event.device); Serial.print('"');
  if (event.kind == RADIO_EVENT_TEST || event.kind == RADIO_EVENT_STATUS) {
    Serial.print(F(",\"model\":\"")); printHexByte(event.model[0]); printHexByte(event.model[1]); Serial.print('"');
  }
  Serial.print(F(",\"event\":\""));
  if (event.kind == RADIO_EVENT_TEST) {
    if (event.alarm == ALARM_FIRE) Serial.print(F("FIRE_TEST"));
    else if (event.alarm == ALARM_CARBON_MONOXIDE) Serial.print(F("CO_TEST"));
    else Serial.print(F("TEST"));
    Serial.print(F("\",\"result\":\"")); Serial.print(event.testPassed ? F("PASS") : F("FAIL"));
    Serial.print(F("\",\"base\":\"ON\""));
    if (event.testPassed) Serial.print(F(",\"battery\":\"OK\""));
  } else if (event.kind == RADIO_EVENT_STATUS) {
    Serial.print(F("STATUS\",\"base\":\"")); Serial.print(event.baseOn ? F("ON") : F("OFF"));
    Serial.print(F("\",\"battery\":\"")); Serial.print(event.batteryLow ? F("LOW") : F("OK")); Serial.print('"');
  } else if (event.kind == RADIO_EVENT_EMERGENCY) {
    if (event.alarm == ALARM_FIRE) Serial.print(F("FIRE_EMERGENCY"));
    else if (event.alarm == ALARM_CARBON_MONOXIDE) Serial.print(F("CO_EMERGENCY"));
    else Serial.print(F("EMERGENCY"));
    Serial.print(F("\",\"base\":\"ON\""));
  } else if (event.kind == RADIO_EVENT_SILENCE) {
    Serial.print(F("SILENCE\",\"base\":\"ON\""));
  } else if (event.kind == RADIO_EVENT_MISSING) {
    Serial.print(F("MISSING\",\"base\":\"MISSING\",\"battery\":\"MISSING\""));
  }
  Serial.print(F(",\"raw_status\":")); Serial.print(event.rawStatus);
  Serial.println('}');
}


void processRadioResponse()
{
  lastHeartBeat = millis();
  RadioEvent event;
  if (decodeRadioEvent(event)) emitV2Event(event);
}



void processHeartBeat()
{
   if (directMode == false) {
  unsigned long currentRunTime = millis();
  if (currentRunTime - lastHeartBeat >= heartBeatInterval)
  {
     printV2Prefix(F("heartbeat"));
     Serial.print(F(",\"uptime\":")); Serial.print(currentRunTime);
     Serial.print(F(",\"radio\":\"")); Serial.print(radioReady ? F("ready") : F("not_ready"));
     Serial.println(F("\"}"));
     lastHeartBeat = currentRunTime;
    }
   }
}



void loop()
{
  serviceRadioFrame();                                      // acquire, validate and dispatch radio traffic first
  ReadByteFromSerial();                                     // read serial buffer and send it to the radio when its ready
  processHeartBeat();
  wdt_reset();
}
