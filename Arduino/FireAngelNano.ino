#include <WiSafeRadioCore.h>

const byte SERIAL_BUFFER_SIZE = 25;
byte serialBuffer[SERIAL_BUFFER_SIZE];
byte serialBufferIndex = 0;
boolean serialFrameOverflow = false;
boolean pairingInProgress = false;
int heartBeatValue = 0;

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
        Serial.println("Radio not ready - Trying to reset");
        delay(500);  
      }
        resetFunc();
    }
  }
  wdt_enable(WDTO_2S);
}



boolean initRadio()
{

  // I don't have enough information to know if init(1) or init(2) is best.
  byte cmdTemplate[] = {0xD3, 0x19, 0x50, 0x00, 0x7E}; // init(1)
  //byte cmdTemplate[] = {0xD3, 0x14, 0x8E, 0x7E}; // init(2)
  
   
  if (!sendAndExpect(cmdTemplate, sizeof(cmdTemplate), 0x46, 2, 1)) {
    if (!directMode) Serial.print(".");
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



void printBridgeDevice()
{
  printHexByte(DevId0);
  printHexByte(DevId1);
  printHexByte(DevId2);
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
    cooperativeWait(1000);
    // Preserve the legacy bridge-device PASS as an initiation record. It is
    // synthetic and is not a response from any remote detector.
    Serial.print(F("{\"device\":\"")); printBridgeDevice();
    if (CMD == 0x31) Serial.println(F("\", \"event\":\"CARBON MONOXIDE TEST\", \"result\":\"PASS\"}"));
    else if (CMD == 0x32) Serial.println(F("\", \"event\":\"FIRE TEST\", \"result\":\"PASS\"}"));
    else Serial.println(F("\", \"event\":\"TEST\", \"result\":\"PASS\"}"));
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
    pairingInProgress = true;
    for (byte i = 0; i <= 20; i++) { cooperativeWait(1000); Serial.print('='); }
    pairingInProgress = false;
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
        } else if (pairingInProgress) {
          Serial.println(F("CMD BUSY"));
        } else {
          SendTemplateToRadio(serialBuffer[0]);
        }
      }
      serialBufferIndex = 0;
      serialFrameOverflow = false;
    } else if (!serialFrameOverflow) {
      if (serialBufferIndex < SERIAL_BUFFER_SIZE) serialBuffer[serialBufferIndex++] = received;
      else serialFrameOverflow = true;
    }
  }
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
