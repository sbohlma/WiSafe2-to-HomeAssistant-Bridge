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
const int BUFFER_SIZE = 25;                       // Buffer for SPI and Serial. All commands must be less than the size this buffer
byte serialBuffer[BUFFER_SIZE];
byte spiBuffer[BUFFER_SIZE];
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
int maxInitAttempts = 50;                        //MAX number of attempts to Init Radio
unsigned int heartBeatInterval = 25000;           // Heartbeat interval (in milliseconds)
unsigned long lastHeartBeat = 0;
int heartBeatValue = 0;
const unsigned long SPI_FRAME_TIMEOUT_MS = 100;
const unsigned long COMMAND_RESPONSE_TIMEOUT_MS = 150;

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
      flushSPIbuffer();
      break;
    }
    radioReinitializationCount++;
    if (i == (maxInitAttempts)) {
      if (directMode == true) {
        Serial.write(0x15);
        Serial.write(0x7E);
      } else {
        Serial.println("Radio not ready - Trying to reset");
        delay(500);  
      }
        resetFunc();
    }
  }
  wdt_enable(WDTO_2S);
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
    Serial.print(".");
    return (false);
  }
  if (directMode == true) {
    Serial.write(0x06);
    Serial.write(0x7E);
  } else {
    Serial.println("INIT OK");
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


void printBridgeDevice()
{
  if (DevId0 <= 0x0F) Serial.print("0"); Serial.print(DevId0, HEX);
  if (DevId1 <= 0x0F) Serial.print("0"); Serial.print(DevId1, HEX);
  if (DevId2 <= 0x0F) Serial.print("0"); Serial.print(DevId2, HEX);
}


void printLegacyCommandEvent(const __FlashStringHelper *event)
{
  Serial.print(F("{\"device\":\""));
  printBridgeDevice();
  Serial.print(F("\", \"event\":\""));
  Serial.print(event);
  Serial.println(F("\"}"));
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
    if (!sendAndExpect(announce, sizeof(announce), 0x41, 2, attempts)) { Serial.println(F("CMD FAIL")); return; }
    byte transmit[] = {0x91, DevId0, DevId1, DevId2, DevModel0, DevModel1, eventCode, 0x05, 0x00, 0x02, 0x7E};
    flushSPIbuffer();
    writeCommand(transmit, sizeof(transmit));
    Serial.println(F("CMD OK"));
    return;
  }

  if (CMD >= 0x34 && CMD <= 0x37) {
    commandType = (CMD <= 0x35) ? 0x50 : 0x61;
    eventCode = (CMD == 0x34) ? 0x41 : ((CMD == 0x35) ? 0x81 : ((CMD == 0x36) ? 0x40 : 0x80));
    byte command[] = {commandType, DevId0, DevId1, DevId2, eventCode, (byte)(commandType == 0x61), 0x7E};
    if (!sendAndExpect(command, sizeof(command), 0x46, 2, attempts)) { Serial.println(F("CMD FAIL")); return; }
    Serial.println(F("CMD OK"));
    cooperativeWait(1000);
    if (CMD == 0x34) printLegacyCommandEvent(F("CARBON MONOXIDE EMERGENCY"));
    else if (CMD == 0x35) printLegacyCommandEvent(F("FIRE EMERGENCY"));
    else printLegacyCommandEvent(F("SILENCE"));
    return;
  }

  byte pairingState[] = {0xD3, 0x03, 0x7E};
  if (CMD == 0x38) {
    if (!sendAndExpect(pairingState, sizeof(pairingState), 0xD4, 11, 6)) { Serial.println(F("CMD FAIL")); return; }
    Serial.println(spiBuffer[2] ? F("NETWORK PAIRED") : F("NETWORK UNPAIRED"));
    return;
  }

  if (CMD == 0x39) {
    if (!sendAndExpect(pairingState, sizeof(pairingState), 0xD4, 11, 4)) { Serial.println(F("CMD FAIL")); return; }
    if (spiBuffer[2]) { Serial.println(F("ERROR NETWORK IS ALREADY PAIRED")); return; }
    byte enablePairing[] = {0xD3, 0x12, 0x01, 0x7E};
    if (!sendAndExpect(enablePairing, sizeof(enablePairing), 0x46, 2, 4) ||
        !sendAndExpect(NULL, 0, 0x41, 2, 1)) { Serial.println(F("CMD FAIL")); return; }
    byte pairingBroadcast[] = {0x91, DevId0, DevId1, DevId2, DevModel0, DevModel1, 0xFF, 0x05, 0x01, 0x01, 0x7E};
    writeCommand(pairingBroadcast, sizeof(pairingBroadcast));
    Serial.println(F("NETWORK PAIRING MODE ACTIVATED"));
    for (byte i = 0; i <= 20; i++) { cooperativeWait(1000); Serial.print('='); }
    Serial.println();
    if (!sendAndExpect(pairingState, sizeof(pairingState), 0xD4, 11, 1)) { Serial.println(F("CMD FAIL(4)")); return; }
    Serial.println(spiBuffer[2] ? F("NETWORK IS NOW PAIRED") : F("NETWORK IS STILL UNPAIRED"));
  }
}



void ReadByteFromSerial()
{
  while (Serial.available() > 0) {
    byte received = Serial.read();
    if (received == 0x0A || received == 0x0D) continue;
    if (received == 0x7E) {
      if (!serialFrameOverflow && serialBufferIndex > 0) {
        if (directMode) {
          writeCommand(serialBuffer, serialBufferIndex);
          WriteByteToRadio(0x7E);
        } else {
          SendTemplateToRadio(serialBuffer[0]);
        }
      }
      serialBufferIndex = 0;
      serialFrameOverflow = false;
    } else if (!serialFrameOverflow) {
      if (serialBufferIndex < BUFFER_SIZE) serialBuffer[serialBufferIndex++] = received;
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
      } else if (spiBufferIndex < BUFFER_SIZE) {
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


void printAlarmName(AlarmKind alarm)
{
  if (alarm == ALARM_FIRE) Serial.print(F("FIRE "));
  else if (alarm == ALARM_CARBON_MONOXIDE) Serial.print(F("CARBON MONOXIDE "));
}


void emitLegacyEvent(const RadioEvent &event)
{
  Serial.print(F("{\"device\":\""));
  printDevice(event.device);
  if (event.kind == RADIO_EVENT_TEST) {
    Serial.print(F("\", \"model\":\"")); printHexByte(event.model[0]); printHexByte(event.model[1]);
    Serial.print(F("\", \"event\":\"")); printAlarmName(event.alarm);
    Serial.print(F("TEST\", \"result\":\""));
    if (event.testPassed) Serial.print(F("PASS\", \"base\":\"ON\", \"battery\":\"OK\""));
    else Serial.print(F("FAIL\", \"base\":\"ON\""));
    Serial.println('}');
  } else if (event.kind == RADIO_EVENT_STATUS) {
    Serial.print(F("\", \"model\":\"")); printHexByte(event.model[0]); printHexByte(event.model[1]);
    Serial.print(event.baseOn ? F("\", \"base\":\"ON") : F("\", \"base\":\"OFF"));
    Serial.println(event.batteryLow ? F("\", \"battery\":\"LOW\"}") : F("\", \"battery\":\"OK\"}"));
  } else if (event.kind == RADIO_EVENT_EMERGENCY) {
    Serial.print(F("\", \"event\":\"")); printAlarmName(event.alarm);
    Serial.println(F("EMERGENCY\", \"base\":\"ON\"}"));
  } else if (event.kind == RADIO_EVENT_SILENCE) {
    Serial.println(F("\", \"event\":\"SILENCE\", \"base\":\"ON\"}"));
  } else if (event.kind == RADIO_EVENT_MISSING) {
    Serial.println(F("\", \"event\":\"MISSING\", \"base\":\"MISSING\", \"battery\":\"MISSING\"}"));
  }
}


void processRadioResponse()
{
  lastHeartBeat = millis();
  RadioEvent event;
  if (decodeRadioEvent(event)) emitLegacyEvent(event);
}



void processHeartBeat()
{
   if (directMode == false) {
  unsigned long currentRunTime = millis();
  if (currentRunTime - lastHeartBeat >= heartBeatInterval) 
  {
     Serial.print("{\"heartBeat\":\"");
     //Serial.print(millis()/heartBeatInterval);
     Serial.print(heartBeatValue);
     Serial.println("\"}");
     lastHeartBeat = currentRunTime;
    if (heartBeatValue < 9){heartBeatValue++;}
    else {heartBeatValue= 0;}
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
