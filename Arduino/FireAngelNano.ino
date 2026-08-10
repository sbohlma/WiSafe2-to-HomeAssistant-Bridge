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
unsigned long spiFrameStartedAt = 0;
boolean directMode = false;
int maxInitAttempts = 50;                        //MAX number of attempts to Init Radio
unsigned int heartBeatInterval = 25000;           // Heartbeat interval (in milliseconds)
unsigned long lastHeartBeat = 0;
int heartBeatValue = 0;
const unsigned long SPI_FRAME_TIMEOUT_MS = 100;
const unsigned long COMMAND_RESPONSE_TIMEOUT_MS = 150;


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
    if (!sendAndExpect(announce, sizeof(announce), 0x41, 2, attempts)) { Serial.println(F("CMD FAIL")); return; }
    byte transmit[] = {0x91, DevId0, DevId1, DevId2, DevModel0, DevModel1, eventCode, 0x05, 0x00, 0x02, 0x7E};
    writeCommand(transmit, sizeof(transmit));
    Serial.println(F("CMD OK"));
    cooperativeWait(1000);
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



void processRadioResponse()
{
lastHeartBeat = millis();                                                                       //Delay the next heartbeat to ensure it doesnt come instantly after a message
  if (!radioReceiveBufferReady || spiBufferIndex < 2 || spiBuffer[spiBufferIndex - 1] != 0x7E) {
    malformedFrameCount++;
    return;
  }
  
  if ((spiBuffer[0] == 0x70) && (spiBufferIndex >= 9))                                           // Test Event
  {
    Serial.print("{\"device\":\"");
    if (spiBuffer[1] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[1]), HEX);
    if (spiBuffer[2] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[2]), HEX);
    if (spiBuffer[3] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[3]), HEX);
    Serial.print("\", \"model\":\"");
    if (spiBuffer[6] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[6]), HEX);
    if (spiBuffer[7] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[7]), HEX);
    Serial.print("\", \"event\":\"");
    if (spiBuffer[4] == 0x81) {Serial.print("FIRE ");}
    if (spiBuffer[4] == 0x82) {Serial.print("FIRE ");}
    if (spiBuffer[4] == 0x41) {Serial.print("CARBON MONOXIDE ");}
    //if (spiBuffer[4] == 0xFF) {Serial.print("ALL");}
    Serial.print("TEST\", \"result\":\"");
    if (spiBuffer[5] == 0x01) {Serial.print("PASS\", \"base\":\"ON\", \"battery\":\"OK\"");} 
    else {Serial.print("FAIL\", \"base\":\"ON\"");} 
    Serial.println("}");
  }

  if ((spiBuffer[0] == 0x71) && (spiBufferIndex >= 8))                                          // Base or Battery Event
  {
    Serial.print("{\"device\":\"");
    if (spiBuffer[1] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[1]), HEX);
    if (spiBuffer[2] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[2]), HEX);
    if (spiBuffer[3] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[3]), HEX);
    Serial.print("\", \"model\":\"");
    if (spiBuffer[4] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[4]), HEX);
    if (spiBuffer[5] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[5]), HEX);
    Serial.print("\", \"base\":\"");

    //byte testBit = 0x04
    if (spiBuffer[6] & 0x04) {Serial.print("ON");} else {Serial.print("OFF");}

    Serial.print("\", \"battery\":\"");
    if (spiBuffer[6] & 0x42) {Serial.print("LOW");} else {Serial.print("OK");}
   // 1 is always set
   // 4 is base on/off
   // 2 and 64 seem to flag a low battery
   
    Serial.println("\"}");
  }

  if ((spiBuffer[0] == 0x50) && (spiBufferIndex >= 7))                                          // Emergency Event
  {
    Serial.print("{\"device\":\"");
    if (spiBuffer[1] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[1]), HEX);
    if (spiBuffer[2] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[2]), HEX);
    if (spiBuffer[3] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[3]), HEX);
    Serial.print("\", \"event\":\"");
    if (spiBuffer[4] == 0x81) {Serial.print("FIRE ");}
    if (spiBuffer[4] == 0x41) {Serial.print("CARBON MONOXIDE ");}
    Serial.println("EMERGENCY\", \"base\":\"ON\"}");
  }

  if ((spiBuffer[0] == 0x61) && (spiBufferIndex >= 7))                                          // Silence Event
  {
    Serial.print("{\"device\":\"");
    if (spiBuffer[1] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[1]), HEX);
    if (spiBuffer[2] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[2]), HEX);
    if (spiBuffer[3] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[3]), HEX);
    Serial.println("\", \"event\":\"SILENCE\", \"base\":\"ON\"}");
  }

  if ((spiBuffer[0] == 0xD2) && (spiBufferIndex >= 10))                                         // Missing Device Event
  {
    Serial.print("{\"device\":\"");
    if (spiBuffer[6] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[6]), HEX);
    if (spiBuffer[7] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[7]), HEX);
    if (spiBuffer[8] <= 0x0F) {Serial.print("0");} Serial.print((spiBuffer[8]), HEX);
    Serial.println("\", \"event\":\"MISSING\", \"base\":\"MISSING\", \"battery\":\"MISSING\"}");
  }
  
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
