#include <WiSafeRadioCore.h>

const byte SERIAL_BUFFER_SIZE = 79;
char serialBuffer[SERIAL_BUFFER_SIZE + 1];
byte serialBufferIndex = 0;
boolean serialFrameOverflow = false;
boolean pairingInProgress = false;
boolean activeCommandHasId = false;
unsigned int activeCommandId = 0;
const char FIRMWARE_VERSION[] = "2.1.0";
const byte SERIAL_PROTOCOL_VERSION = 2;

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



const __FlashStringHelper *commandName(byte command)
{
  switch (command) {
    case 0x31: return F("sound_co");
    case 0x32: return F("sound_fire");
    case 0x33: return F("sound_combined");
    case 0x34: return F("co_emergency");
    case 0x35: return F("fire_emergency");
    case 0x36: return F("silence_co");
    case 0x37: return F("silence_fire");
    case 0x38: return F("pairing_state");
    case 0x39: return F("pairing");
    case 0x3A: return F("co_emergency_stop");
    case 0x3B: return F("fire_emergency_stop");
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


void printV2ErrorForRequest(const __FlashStringHelper *code, boolean requestHasId,
                            unsigned int requestId)
{
  printV2Prefix(F("error"));
  if (requestHasId) { Serial.print(F(",\"id\":")); Serial.print(requestId); }
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
    // This acknowledgement comes from the attached donor radio only. Remote
    // detectors sound the interlink test signal but do not send test results.
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

  if (CMD == 0x3A || CMD == 0x3B) {
    // Alarm stop as sent by FireAngel's Mesh Monitor software; not yet verified on a network.
    byte command[] = {0x51, DevId0, DevId1, DevId2, (byte)((CMD == 0x3A) ? 0x41 : 0x81), 0x7E};
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
    pairingInProgress = true;
    for (byte i = 0; i <= 20; i++) cooperativeWait(1000);
    pairingInProgress = false;
    if (!sendAndExpect(pairingState, sizeof(pairingState), 0xD4, 11, 1)) {
      printV2Error(F("pairing_state_timeout"));
      return;
    }
    printCommandResult(CMD, spiBuffer[2] ? F("paired") : F("unpaired"));
  }
}



boolean parseRequestId(const char *json, unsigned int &requestId)
{
  // Deliberately parse only HA's bounded, fixed command schema. This avoids a
  // dynamic JSON dependency on the AVR; it is not a general-purpose parser.
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
  // See parseRequestId(): keys are found by name in trusted command objects.
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
  if (!strcmp(name, "sound_co")) command = 0x31;
  else if (!strcmp(name, "sound_fire")) command = 0x32;
  else if (!strcmp(name, "sound_combined")) command = 0x33;
  else if (!strcmp(name, "co_emergency")) command = 0x34;
  else if (!strcmp(name, "fire_emergency")) command = 0x35;
  else if (!strcmp(name, "co_emergency_stop")) command = 0x3A;
  else if (!strcmp(name, "fire_emergency_stop")) command = 0x3B;
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
  if (!directMode && pairingInProgress) {
    boolean idPresent = strstr(serialBuffer, "\"id\"") != NULL;
    unsigned int requestId = 0;
    boolean requestHasId = parseRequestId(serialBuffer, requestId);
    if (idPresent && !requestHasId) printV2ErrorForRequest(F("invalid_id"), false, 0);
    else printV2ErrorForRequest(F("busy"), requestHasId, requestId);
  } else if (directMode) {
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



void serviceSerialInputDuringWait()
{
  if (pairingInProgress) ReadByteFromSerial();
}


void loop()
{
  serviceRadioFrame();                                      // acquire, validate and dispatch radio traffic first
  ReadByteFromSerial();                                     // read serial buffer and send it to the radio when its ready
  processHeartBeat();
  wdt_reset();
}
