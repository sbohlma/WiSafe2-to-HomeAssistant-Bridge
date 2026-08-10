#include<SPI.h>
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
byte spiBufferIndex = 0;
int spiBufferDumpCounter = 0;
boolean radioReceiveBufferReady = false;
boolean spiFrameOverflow = false;
unsigned int spiOverflowCount = 0;
unsigned int malformedFrameCount = 0;
boolean directMode = false;
int maxInitAttempts = 50;                        //MAX number of attempts to Init Radio
unsigned int heartBeatInterval = 25000;           // Heartbeat interval (in milliseconds)
unsigned long lastHeartBeat = 0;
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
  
   
  sendCMDToRadio(cmdTemplate, sizeof(cmdTemplate), true);
  delay(500);
  if ((spiBufferIndex != 2) || (spiBuffer[0] != 0x46) || (spiBuffer[1] != 0x7E)) {
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
  if (spiBufferIndex > 0)
  { // check the SPI incoming buffer isn't stagnant
    spiBufferDumpCounter++;
    if (spiBufferDumpCounter > 1000)
    {
      flushSPIbuffer();
    }
  }
}



void flushSPIbuffer()
{
  spiBufferIndex = 0;
  memset(spiBuffer, 0, sizeof(spiBuffer));
  spiBufferDumpCounter = 0;
  radioReceiveBufferReady = false;
  spiFrameOverflow = false;
}


boolean responseIs(byte firstByte, byte expectedLength)
{
  return (radioReceiveBufferReady && spiBufferIndex == expectedLength &&
          spiBuffer[0] == firstByte && spiBuffer[expectedLength - 1] == 0x7E);
}



void SendTemplateToRadio(byte CMD)
{
int retries = 0;
retry:
lastHeartBeat = millis();                                                                                          //Delay the next heartbeat to ensure it doesnt come instantly after a message

  if (CMD == (0x31))                                                                                               //TEST - CO
  {
    retries++;
    byte cmdTemplate[] = {0x70, DevId0, DevId1, DevId2, 0x41, 0x01, DevModel0, DevModel1, 0x7E};
    byte cmdTemplate2[] = {0x91, DevId0, DevId1, DevId2, DevModel0, DevModel1, 0x41, 0x05, 0x00, 0x02, 0x7E};
    sendCMDToRadio(cmdTemplate, sizeof(cmdTemplate), true);
    if (!responseIs(0x41, 2)) {
      if (retries > 3) {Serial.println("CMD FAIL");return;} else {delay (500); goto retry; }
    }
    sendCMDToRadio(cmdTemplate2, sizeof(cmdTemplate2), true);
    Serial.println("CMD OK");
    delay (1000);
    Serial.print("{\"device\":\"");
    if (DevId0 <= 0x0F) {Serial.print("0");} Serial.print((DevId0), HEX);
    if (DevId1 <= 0x0F) {Serial.print("0");} Serial.print((DevId1), HEX);
    if (DevId2 <= 0x0F) {Serial.print("0");} Serial.print((DevId2), HEX);
    Serial.println("\", \"event\":\"CARBON MONOXIDE TEST\", \"result\":\"PASS\"}"); 
  }
  
  if (CMD == (0x32))                                                                                               //TEST - SMOKE
  {
    retries++;
    byte cmdTemplate[] = {0x70, DevId0, DevId1, DevId2, 0x81, 0x01, DevModel0, DevModel1, 0x7E};
    byte cmdTemplate2[] = {0x91, DevId0, DevId1, DevId2, DevModel0, DevModel1, 0x81, 0x05, 0x00, 0x02, 0x7E};
    sendCMDToRadio(cmdTemplate, sizeof(cmdTemplate), true);
    if (!responseIs(0x41, 2)) {
      if (retries > 3) {Serial.println("CMD FAIL");return;} else {delay (500); goto retry; }
    }
    sendCMDToRadio(cmdTemplate2, sizeof(cmdTemplate2), true);
    Serial.println("CMD OK");
    delay (1000);
    Serial.print("{\"device\":\"");
    if (DevId0 <= 0x0F) {Serial.print("0");} Serial.print((DevId0), HEX);
    if (DevId1 <= 0x0F) {Serial.print("0");} Serial.print((DevId1), HEX);
    if (DevId2 <= 0x0F) {Serial.print("0");} Serial.print((DevId2), HEX);
    Serial.println("\", \"event\":\"FIRE TEST\", \"result\":\"PASS\"}"); 
  }
  
  if (CMD == (0x33))                                                                                              //TEST - SMOKE+CO
  {
    retries++;
    byte cmdTemplate[] = {0x70, DevId0, DevId1, DevId2, 0xFF, 0x01, DevModel0, DevModel1, 0x7E};
    byte cmdTemplate2[] = {0x91, DevId0, DevId1, DevId2, DevModel0, DevModel1, 0xFF, 0x05, 0x00, 0x02, 0x7E};
    sendCMDToRadio(cmdTemplate, sizeof(cmdTemplate), true);
    if (!responseIs(0x41, 2)) {
     if (retries > 3) {Serial.println("CMD FAIL");return;} else {delay (500); goto retry; }
    }
    sendCMDToRadio(cmdTemplate2, sizeof(cmdTemplate2), true);
    Serial.println("CMD OK");
    delay (1000);
    Serial.print("{\"device\":\"");
    if (DevId0 <= 0x0F) {Serial.print("0");} Serial.print((DevId0), HEX);
    if (DevId1 <= 0x0F) {Serial.print("0");} Serial.print((DevId1), HEX);
    if (DevId2 <= 0x0F) {Serial.print("0");} Serial.print((DevId2), HEX);
    Serial.println("\", \"event\":\"TEST\", \"result\":\"PASS\"}"); 
  }
  
  if (CMD == (0x34))                                                                                              //EMERGENCY - CO
  {
    retries++;
    byte cmdTemplate[] = {0x50, DevId0, DevId1, DevId2, 0x41, 0x00, 0x7E};
    sendCMDToRadio(cmdTemplate, sizeof(cmdTemplate), true);
    if (!responseIs(0x46, 2)) {
      if (retries > 5) {Serial.println("CMD FAIL");return;} else {delay (500); goto retry; }
    }
    Serial.println("CMD OK");
    delay (1000);
    Serial.print("{\"device\":\"");
    if (DevId0 <= 0x0F) {Serial.print("0");} Serial.print((DevId0), HEX);
    if (DevId1 <= 0x0F) {Serial.print("0");} Serial.print((DevId1), HEX);
    if (DevId2 <= 0x0F) {Serial.print("0");} Serial.print((DevId2), HEX);
    Serial.println("\", \"event\":\"CARBON MONOXIDE EMERGENCY\"}"); 
  }
  
  if (CMD == (0x35))                                                                                            //EMERGENCY - SMOKE
  {
    retries++;
    byte cmdTemplate[] = {0x50, DevId0, DevId1, DevId2, 0x81, 0x00, 0x7E};
    sendCMDToRadio(cmdTemplate, sizeof(cmdTemplate), true);
    if (!responseIs(0x46, 2)) {
       if (retries > 5) {Serial.println("CMD FAIL");return;} else {delay (500); goto retry; }
    }
    Serial.println("CMD OK");
    delay (1000);
    Serial.print("{\"device\":\"");
    if (DevId0 <= 0x0F) {Serial.print("0");} Serial.print((DevId0), HEX);
    if (DevId1 <= 0x0F) {Serial.print("0");} Serial.print((DevId1), HEX);
    if (DevId2 <= 0x0F) {Serial.print("0");} Serial.print((DevId2), HEX);
    Serial.println("\", \"event\":\"FIRE EMERGENCY\"}"); 
  }
  
  if (CMD == (0x36))                                                                                            //SILENCE - (As CO device)
  {
    retries++;
    byte cmdTemplate[] = {0x61, DevId0, DevId1, DevId2, 0x40, 0x01, 0x7E};
    sendCMDToRadio(cmdTemplate, sizeof(cmdTemplate), true);
    if (!responseIs(0x46, 2)) {
      if (retries > 5) {Serial.println("CMD FAIL");return;} else {delay (500); goto retry; }
    }
    Serial.println("CMD OK");
    delay (1000);
    Serial.print("{\"device\":\"");
    if (DevId0 <= 0x0F) {Serial.print("0");} Serial.print((DevId0), HEX);
    if (DevId1 <= 0x0F) {Serial.print("0");} Serial.print((DevId1), HEX);
    if (DevId2 <= 0x0F) {Serial.print("0");} Serial.print((DevId2), HEX);
    Serial.println("\", \"event\":\"SILENCE\"}"); 
  }
  
  if (CMD == (0x37))                                                                                          //SILENCE - (As SMOKE device)
  {
    retries++;
    byte cmdTemplate[] = {0x61, DevId0, DevId1, DevId2, 0x80, 0x01, 0x7E};
    sendCMDToRadio(cmdTemplate, sizeof(cmdTemplate), true);
    if (!responseIs(0x46, 2)) {
    if (retries > 5) {Serial.println("CMD FAIL");return;} else {delay (500); goto retry; }
    }
    Serial.println("CMD OK");
    delay (1000);
    Serial.print("{\"device\":\"");
    if (DevId0 <= 0x0F) {Serial.print("0");} Serial.print((DevId0), HEX);
    if (DevId1 <= 0x0F) {Serial.print("0");} Serial.print((DevId1), HEX);
    if (DevId2 <= 0x0F) {Serial.print("0");} Serial.print((DevId2), HEX);
    Serial.println("\", \"event\":\"SILENCE\"}"); 
  }
  
  if (CMD == (0x38))                                                                                          //GET PAIRING STATE
  {
    retries++;
    byte cmdTemplate[] = {0xD3, 0x03, 0x7E};
    sendCMDToRadio(cmdTemplate, sizeof(cmdTemplate), true);
    if (!responseIs(0xD4, 11)) {
      if (retries > 5) {Serial.println("CMD FAIL");return;} else {delay (500); goto retry; }
    }
    if (spiBuffer[2] != (0x00)) {
      Serial.println("NETWORK PAIRED");
      return;
    }
    Serial.println("NETWORK UNPAIRED");
  }
  
  if (CMD == (0x39))                                                                                            //START PAIRING MODE
  {
    retries++;
    byte cmdTemplate[] = {0xD3, 0x03, 0x7E};
    sendCMDToRadio(cmdTemplate, sizeof(cmdTemplate), true);
    if (!responseIs(0xD4, 11)) {
      if (retries > 3) {Serial.println("CMD FAIL");return;} else {delay (500); goto retry; }
    }
    if (spiBuffer[2] != (0x00)) {
      Serial.println("ERROR NETWORK IS ALREADY PAIRED");
      return;
    }
    flushSPIbuffer();
    retries++;
    byte cmdTemplate2[] = {0xD3, 0x12, 0x01, 0x7E};
    byte cmdTemplate3[] = {0x91, DevId0, DevId1, DevId2, DevModel0, DevModel1, 0xFF, 0x05, 0x01, 0x01, 0x7E};
    sendCMDToRadio(cmdTemplate2, sizeof(cmdTemplate2), true);
    if (!responseIs(0x46, 2)) {
       if (retries > 4) {Serial.println("CMD FAIL");return;} else {delay (500); goto retry; }
    }
    flushSPIbuffer();
    retries++;
    sendCMDToRadio(NULL, 0, true);
    if (!responseIs(0x41, 2)) {
       if (retries > 5) {Serial.println("CMD FAIL");return;} else {delay (500); goto retry; }
    }
    sendCMDToRadio(cmdTemplate3, sizeof(cmdTemplate3), true);
    Serial.println("NETWORK PAIRING MODE ACTIVATED");
    for (int i = 0; i <= 20; i++) {
      delay(1000);
      Serial.print("=");
    }
    flushSPIbuffer();
    Serial.println("");
    sendCMDToRadio(cmdTemplate, sizeof(cmdTemplate), true);
    if (!responseIs(0xD4, 11)) {
      Serial.println("CMD FAIL(4)");
      return;
    }
    if (spiBuffer[2] != (0x00)) {
      Serial.println("NETWORK IS NOW PAIRED");
      return;
    }
    Serial.println("NETWORK IS STILL UNPAIRED");
  }
}



void sendCMDToRadio(byte cmd[], int cmdSize, boolean waitForReply)
{
  int cmdTTL = 0;
  // A command attempt must never validate a response left by an earlier attempt.
  // Process a completed unsolicited frame before starting a fresh transaction.
  if (radioReceiveBufferReady) {
    if (directMode) {
      Serial.write(spiBuffer, spiBufferIndex);
    } else {
      processRadioResponse();
    }
  }
  flushSPIbuffer();
  for (byte i = 0; i < cmdSize; i++) {
    WriteByteToRadio(cmd[i]);
  }
  if (waitForReply == true)
  {
    while ((radioReceiveBufferReady == false) && (cmdTTL < 2500))
    {
      ReadByteFromRadio();
      cmdTTL++;
      delayMicroseconds(50);
    }
  }
}



void ReadByteFromSerial()
{ // check for incoming byte from Serial
  if (Serial.available() > 0) {
    delay(4);                                                                   // wait for transmission (Buffer = 25 | 9600bps = 1.04ms/char =30ms | 115200bps = 0.097ms/char =3ms)
    int rlen = Serial.readBytesUntil(0x7E, serialBuffer, BUFFER_SIZE);          // read 25 bytes, until timeout, or 7E char
    boolean terminated = (rlen < BUFFER_SIZE);
    if (!terminated) {
      // Discard an overlong command through its terminator; never treat a prefix
      // as a valid command or append a synthetic terminator to it.
      while (Serial.available()) {
        if (Serial.read() == 0x7E) break;
      }
      rlen = 0;
    }
    //   if ((serialBuffer[0] == (0x3E)) && (serialBuffer[1] == (0x3E)))
    if ((rlen > 0) && (directMode == false))
    {
      SendTemplateToRadio(serialBuffer[0]);
      delay(500);                                                               // avoid returning to the main loop too soon (which will flush SPI buffer)
    }
    else if (rlen > 0)
    {
      for (int i = 0; i < rlen; i++)                                            // send raw hex to radio
      {
        WriteByteToRadio(serialBuffer[i]);                                      // send data to the radio... could check it for special commands first
      }
      WriteByteToRadio(0x7E);                                                   // replace the 7E char (removed by serial.readBytesUntil)
    }
  }                                         
  if (Serial.available()) {                                                     //Send carriage return characters to a black hole if they've followed the 0x7E terminator
    if ((Serial.peek() == (0x0A)) || (Serial.peek() == (0x0D))) {
      Serial.read();
    }
  }
  if (Serial.available()) {
    if ((Serial.peek() == (0x0A)) || (Serial.peek() == (0x0D))) {
      Serial.read();
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
        spiBuffer[spiBufferIndex++] = Slavereceived;
        spiBufferDumpCounter = 0;
        if (Slavereceived == 0x7E) radioReceiveBufferReady = true;
      } else {
        spiOverflowCount++;
        spiFrameOverflow = true;
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
  ReadByteFromRadio();                                      // check for incoming SPI byte from Radio to SPIbuffer (SPI master)
  if (radioReceiveBufferReady == true)
  { // write bytes from SPIbuffer to Serial
    if (directMode == true) {
      Serial.write(spiBuffer, spiBufferIndex);
    }
    else
    {
      processRadioResponse();                                //process the response
    }
    flushSPIbuffer();
  }
  checkIfSPIbufferIsStagnant();                             // check if incoming SPI data is incomplete
  ReadByteFromSerial();                                     // read serial buffer and send it to the radio when its ready
  processHeartBeat();
}
