// Test firmware for the bridge hardware: a line-based serial console to the
// WiSafe2 radio module (RM) for protocol experiments. Not meant for Home
// Assistant. Same Nano, PCB and wiring as the other images; the direct-mode
// jumper is ignored.
//
// Differences from the bridge images, all from observations on a real network:
// - Nothing is sent at startup. The old images sent D3 19 50 00 at every
//   reset, and the RM flooded it through the whole network.
// - Frames to the RM are escaped (7E as 7D 01, 7D as 7D 02).
// - A frame the RM does not clock (after an init, during open remote
//   requests) is repeated for up to 60 s instead of being lost byte by byte.
// - Every 41 is answered with 91; C1, C3 and 72/C5/C7 for our own device ID
//   are answered like a detector would.
// - Copies of network messages (same SID, type and sequence number) are
//   marked and not answered twice.
// - Bytes from the RM are acknowledged in an interrupt, independent of serial
//   output.

#include <EEPROM.h>
#include <avr/wdt.h>

#include "w2_protocol.h"

const char FIRMWARE_VERSION[] = "1.0.0";

// Bridge PCB: D10/PB2 chip select from the RM, D11 MOSI, D12/PB4 MISO, D13 SCK,
// D9/PB1 IRQ to the RM.
const uint8_t IRQ_BIT = _BV(PB1);
const uint8_t CS_BIT = _BV(PB2);
const uint8_t MISO_BIT = _BV(PB4);

const unsigned int CS_WAIT_US = 5000;
// 46/47 arrive after about 60 ms, D2 and D4 03 after about 70 ms.
const unsigned long ACK_TIMEOUT_MS = 150;
const unsigned long LOCAL_REPLY_TIMEOUT_MS = 500;
// Commands sent too close to another frame got lost or cut short.
const unsigned long QUIET_MS = 500;
const unsigned long RETRY_DELAY_MS = 500;
const uint8_t SEND_ATTEMPTS = 120;
const uint16_t HOST_BATTERY_MV = 3000;
const uint8_t LINE_MAX = 63;
const uint8_t HOST_QUEUE = 4;

// Types come before the first function: the Arduino builder inserts its
// generated prototypes there.

enum SendError : uint8_t { SEND_OK, SEND_CS_BUSY, SEND_NO_CLOCK, SEND_INCOMPLETE };

struct Settings {
  uint16_t magic;
  w2::Identity identity;
  uint8_t autoReply;
};

enum RequestKind : uint8_t {
  KIND_HOST_REPLY,  // answer to the RM, e.g. 91 to 41; before all other requests
  KIND_LOCAL,       // answered by the RM itself (D1, D3 03), without 46
  KIND_REMOTE,      // goes over the air; 46 or 47
};

struct Request {
  RequestKind kind;
  uint8_t len;
  uint8_t data[w2::MAX_PAYLOAD];
  bool expectsAck;
  // KIND_LOCAL: the answer starts with replyCode, and replySub if not 0.
  uint8_t replyCode;
  uint8_t replySub;
  // KIND_HOST_REPLY: the request that is answered.
  uint8_t cause;
  uint8_t attempts;
};

enum Phase : uint8_t { PHASE_IDLE, PHASE_AWAIT };

//------------------------------------------------------------------------------
// SPI slave

const uint8_t RX_RING = 64;
volatile uint8_t rxRing[RX_RING];
volatile uint8_t rxHead = 0;
volatile uint8_t rxTail = 0;
volatile uint8_t rxDropped = 0;
volatile bool txActive = false;

// The RM frames every byte with chip select and waits for an 8 us IRQ pulse
// after chip select rises before it sends the next byte.
ISR(PCINT0_vect)
{
  if (!(PINB & CS_BIT) || txActive) return;
  for (uint8_t i = 0; i < 20 && !(SPSR & _BV(SPIF)); i++) delayMicroseconds(1);
  if (SPSR & _BV(SPIF)) {
    const uint8_t received = SPDR;
    SPDR = 0;
    const uint8_t next = (rxHead + 1) % RX_RING;
    if (next != rxTail) {
      rxRing[rxHead] = received;
      rxHead = next;
    } else if (rxDropped < 255) {
      rxDropped++;
    }
  }
  PORTB |= IRQ_BIT;
  delayMicroseconds(8);
  PORTB &= ~IRQ_BIT;
}

bool popRxByte(uint8_t &value)
{
  if (rxTail == rxHead) return false;
  value = rxRing[rxTail];
  rxTail = (rxTail + 1) % RX_RING;
  return true;
}

void setupSpiSlave()
{
  SPCR = _BV(SPE);  // slave, mode 0, MSB first
  SPDR = 0;
  DDRB |= MISO_BIT;
  PCMSK0 = _BV(PCINT2);
  PCIFR = _BV(PCIF0);
  PCICR |= _BV(PCIE0);
}

bool waitCs(bool high, unsigned int timeoutUs)
{
  const unsigned long start = micros();
  while (((PINB & CS_BIT) != 0) != high) {
    if (micros() - start >= timeoutUs) return false;
  }
  return true;
}

// IRQ stays high for one byte; the RM clocks it out of SPDR and releases chip
// select.
SendError writeByte(uint8_t value)
{
  if (!waitCs(true, CS_WAIT_US)) return SEND_CS_BUSY;
  delayMicroseconds(10);
  txActive = true;
  if (SPSR & _BV(SPIF)) (void)SPDR;
  SPDR = value;
  PORTB |= IRQ_BIT;
  const bool started = waitCs(false, CS_WAIT_US);
  const bool finished = started && waitCs(true, CS_WAIT_US);
  PORTB &= ~IRQ_BIT;
  if (SPSR & _BV(SPIF)) (void)SPDR;
  if (PINB & CS_BIT) SPDR = 0;
  PCIFR = _BV(PCIF0);
  txActive = false;
  if (!started) return SEND_NO_CLOCK;
  return finished ? SEND_OK : SEND_INCOMPLETE;
}

//------------------------------------------------------------------------------
// Settings

const uint16_t SETTINGS_MAGIC = 0x5431;
Settings settings;

void loadSettings()
{
  EEPROM.get(0, settings);
  if (settings.magic == SETTINGS_MAGIC) return;
  // Identity of the old bridge images (WST-630); the RM at SID 16 learned it.
  settings.identity.device[0] = 0xA5;
  settings.identity.device[1] = 0xB8;
  settings.identity.device[2] = 0x13;
  settings.identity.model = 785;
  settings.identity.deviceClass = 0xFF;
  settings.autoReply = 1;
}

void saveSettings()
{
  settings.magic = SETTINGS_MAGIC;
  EEPROM.put(0, settings);
}

//------------------------------------------------------------------------------
// Output

void printHex2(uint8_t value)
{
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

void printHexBytes(const uint8_t *data, uint8_t len)
{
  for (uint8_t i = 0; i < len; i++) {
    if (i) Serial.print(' ');
    printHex2(data[i]);
  }
}

// Packet order 3D E4 2F is shown as "2FE43D", as in the FireAngel tools.
void printDevice(const uint8_t *device)
{
  printHex2(device[2]);
  printHex2(device[1]);
  printHex2(device[0]);
}

void printModel(const uint8_t *packed)
{
  Serial.print(packed[0] | (packed[1] << 8));
}

void printSidMap(const uint8_t *map)
{
  bool any = false;
  for (uint8_t sid = 0; sid <= w2::MAX_SID; sid++) {
    if (!(map[sid / 8] & (1 << (sid % 8)))) continue;
    Serial.print(' ');
    Serial.print(sid);
    any = true;
  }
  if (!any) Serial.print(F(" none"));
}

void printField(const __FlashStringHelper *name)
{
  Serial.print(F(", "));
  Serial.print(name);
  Serial.print(' ');
}

void describeRmReport(const uint8_t *d, uint8_t len)
{
  switch (len >= 2 ? d[1] : 0) {
    case 0x01:
      if (len < 11) break;
      Serial.print(F("unreachable, reason "));
      printHex2(d[2]);
      Serial.print(F(", SIDs"));
      printSidMap(&d[3]);
      return;
    case 0x03:
      if (len < 10) break;
      Serial.print(F("local SID map:"));
      printSidMap(&d[2]);
      return;
    case 0x04:
      if (len < 10) break;
      Serial.print(F("new network map:"));
      printSidMap(&d[2]);
      return;
    case 0x06:
      if (len < 13) break;
      Serial.print(F("remote status SID "));
      Serial.print(d[2]);
      printField(F("RM battery"));
      Serial.print(w2::rmBatteryMv(d[3]));
      Serial.print(F(" mV"));
      printField(F("RSSI"));
      Serial.print(d[6]);
      printField(F("FW"));
      printHex2(d[7]);
      printField(F("ID"));
      printDevice(&d[8]);
      printField(F("critical"));
      printHex2(d[11]);
      printField(F("radio faults"));
      Serial.print(d[12]);
      return;
    case 0x09:
      if (len < 6) break;
      Serial.print(F("presence SID "));
      Serial.print(d[2]);
      printField(F("device"));
      printDevice(&d[3]);
      return;
    case 0x10:
      if (len < 11) break;
      Serial.print(F("init report, own SID "));
      Serial.print(d[10]);
      Serial.print(F(", answered:"));
      printSidMap(&d[2]);
      return;
    case 0x11:
      if (len < 12) break;
      Serial.print(F("remote SID map of SID "));
      Serial.print(d[11]);
      Serial.print(':');
      printSidMap(&d[3]);
      return;
  }
  Serial.print(F("RM report"));
}

void describeRadio(const uint8_t *d, uint8_t len)
{
  switch (d[0]) {
    case 0x50: Serial.print(F("ALARM")); break;
    case 0x51: Serial.print(F("alarm stop")); break;
    case 0x61: Serial.print(F("silence")); break;
    case 0x70: Serial.print(F("test")); break;
    case 0x71: Serial.print(F("fault report")); break;
    case 0x72: Serial.print(F("fault details request")); break;
    case 0x73: Serial.print(F("fault details")); break;
    case 0xC1: Serial.print(F("diagnose request")); break;
    case 0xC2: Serial.print(F("diagnose result")); break;
    case 0xC3: Serial.print(F("ID request")); break;
    case 0xC4: Serial.print(F("ID info")); break;
    case 0xC5: Serial.print(F("fault events request")); break;
    case 0xC6: Serial.print(F("fault events")); break;
    case 0xC7: Serial.print(F("battery request")); break;
    case 0xC8: Serial.print(F("battery")); break;
    default: Serial.print(F("message")); break;
  }
  switch (d[0]) {
    case 0x50:
    case 0x51:
    case 0x61:
      printField(F("device"));
      printDevice(&d[1]);
      printField(F("class"));
      printHex2(d[4]);
      break;
    case 0x70:
      printField(F("device"));
      printDevice(&d[1]);
      printField(F("class"));
      printHex2(d[4]);
      Serial.print(d[5] == 0x01 ? F(", OK") : F(", FAILED"));
      printField(F("model"));
      printModel(&d[6]);
      break;
    case 0x71:
      printField(F("device"));
      printDevice(&d[1]);
      printField(F("model"));
      printModel(&d[4]);
      printField(F("flags"));
      printHex2(d[6]);
      break;
    case 0x72:
    case 0xC5:
    case 0xC7:
      printField(F("for device"));
      printDevice(&d[1]);
      printField(F("model"));
      printModel(&d[4]);
      break;
    case 0x73:
      printField(F("device"));
      printDevice(&d[1]);
      printField(F("FS"));
      printHex2(d[7]);
      printHex2(d[6]);
      break;
    case 0xC2:
    case 0xC4:
      printField(F("device"));
      printDevice(&d[1]);
      printField(F("class"));
      printHex2(d[4]);
      printField(F("model"));
      printModel(&d[5]);
      if (d[0] == 0xC2) {
        printField(F("result"));
        printHex2(d[7]);
      }
      break;
    case 0xC6:
      printField(F("device"));
      printDevice(&d[1]);
      printField(F("events"));
      printHexBytes(&d[5], 3);
      break;
    case 0xC8:
      printField(F("device"));
      printDevice(&d[1]);
      Serial.print(F(", "));
      Serial.print(d[5] | (d[6] << 8));
      Serial.print(F(" mV"));
      break;
  }
  printField(F("SID"));
  Serial.print(w2::radioSid(d, len));
  printField(F("seq"));
  Serial.print(w2::radioSeq(d, len));
}

void describe(const uint8_t *d, uint8_t len)
{
  if (len == 1 && d[0] == w2::MSG_ACK) {
    Serial.print(F("ack"));
  } else if (len == 1 && d[0] == w2::MSG_REJECT) {
    Serial.print(F("rejected, SID not in the local map"));
  } else if (len == 1 && d[0] == w2::MSG_IDENTITY_REQUEST) {
    Serial.print(F("RM asks for the host identity"));
  } else if (d[0] == w2::MSG_RM_STATUS && len >= 12) {
    Serial.print(F("RM status SID "));
    Serial.print(d[11]);
    if (d[11] == w2::SID_UNPAIRED) Serial.print(F(" (unpaired)"));
    printField(F("FW"));
    printHex2(d[5]);
    printField(F("ID"));
    printDevice(&d[6]);
    printField(F("RM battery"));
    Serial.print(w2::rmBatteryMv(d[1]));
    Serial.print(F(" mV"));
    printField(F("RSSI"));
    Serial.print(d[4]);
    printField(F("critical"));
    printHex2(d[9]);
    printField(F("radio faults"));
    Serial.print(d[10]);
  } else if (d[0] == w2::MSG_RM_REPORT) {
    describeRmReport(d, len);
  } else if (w2::isRadioMessage(d, len)) {
    describeRadio(d, len);
  } else {
    Serial.print(F("unknown"));
  }
}

void printStamp(const __FlashStringHelper *tag)
{
  Serial.print(tag);
  Serial.print(' ');
  Serial.print(millis());
  Serial.print(' ');
}

//------------------------------------------------------------------------------
// Requests to the RM

w2::FrameDecoder decoder;
w2::DuplicateFilter duplicates;
w2::ReplyLimiter limiter;

Request hostQueue[HOST_QUEUE];
uint8_t hostCount = 0;
Request userRequest;
bool userPending = false;
Request *active = NULL;
Phase phase = PHASE_IDLE;
unsigned long phaseSince = 0;
unsigned long retryAt = 0;
unsigned long lastFrameMs = 0;

uint8_t ownSid = 0xFF;
unsigned int rxFrames = 0;
unsigned int brokenFrames = 0;
unsigned int copies = 0;
unsigned int lostBytes = 0;
unsigned int txOk = 0;
unsigned int txFailed = 0;

Request makeRequest(const uint8_t *data, uint8_t len)
{
  Request request;
  memset(&request, 0, sizeof(request));
  memcpy(request.data, data, len);
  request.len = len;
  request.kind = KIND_REMOTE;
  request.expectsAck = true;
  if (len == 1 && data[0] == w2::MSG_RM_STATUS_REQUEST) {
    request.kind = KIND_LOCAL;
    request.expectsAck = false;
    request.replyCode = w2::MSG_RM_STATUS;
  } else if (len == 2 && data[0] == w2::MSG_RM_COMMAND && data[1] == 0x03) {
    request.kind = KIND_LOCAL;
    request.expectsAck = false;
    request.replyCode = w2::MSG_RM_REPORT;
    request.replySub = 0x03;
  } else if (data[0] == w2::MSG_IDENTITY_REPLY) {
    request.expectsAck = false;
  }
  return request;
}

void queueHostReply(const uint8_t *data, uint8_t len, uint8_t cause)
{
  if (hostCount >= HOST_QUEUE) {
    Serial.println(F("E host answer dropped, queue full"));
    return;
  }
  Request request = makeRequest(data, len);
  request.kind = KIND_HOST_REPLY;
  request.expectsAck = data[0] != w2::MSG_IDENTITY_REPLY;
  request.cause = cause;
  hostQueue[hostCount++] = request;
}

void submit(const uint8_t *data, uint8_t len)
{
  if (userPending) {
    Serial.println(F("E busy, wait for the TX result"));
    return;
  }
  userRequest = makeRequest(data, len);
  userPending = true;
}

void printRequest(const Request &request)
{
  printStamp(request.kind == KIND_HOST_REPLY ? F("AUTO") : F("TX"));
  printHexBytes(request.data, request.len);
  if (request.kind == KIND_HOST_REPLY) {
    Serial.print(F(" (to "));
    printHex2(request.cause);
    Serial.print(')');
  }
  Serial.print(F(": "));
}

void finishActive(const __FlashStringHelper *result, bool ok)
{
  printRequest(*active);
  Serial.println(result);
  if (ok) txOk++;
  else txFailed++;
  if (active == &userRequest) {
    userPending = false;
  } else {
    hostCount--;
    for (uint8_t i = 0; i < hostCount; i++) hostQueue[i] = hostQueue[i + 1];
  }
  active = NULL;
  phase = PHASE_IDLE;
}

void sendActive(unsigned long now)
{
  uint8_t encoded[w2::MAX_ENCODED];
  const uint8_t len = w2::encodeFrame(active->data, active->len, encoded, sizeof(encoded));
  uint8_t sent = 0;
  SendError error = SEND_OK;
  while (sent < len) {
    error = writeByte(encoded[sent]);
    if (error != SEND_OK) break;
    sent++;
  }
  if (error != SEND_OK && sent == 0) {
    // The RM takes no bytes for a while after an init and during open remote
    // requests.
    active->attempts++;
    if (active->attempts >= SEND_ATTEMPTS) {
      finishActive(F("failed, the RM did not clock for 60 s"), false);
      return;
    }
    if (active->attempts == 1) {
      printRequest(*active);
      Serial.println(F("the RM does not clock, retrying"));
    }
    retryAt = now + RETRY_DELAY_MS;
    active = NULL;
    return;
  }
  lastFrameMs = millis();
  if (error != SEND_OK) {
    finishActive(F("failed, frame cut off"), false);
    return;
  }
  if (active->expectsAck || active->kind == KIND_LOCAL) {
    phase = PHASE_AWAIT;
    phaseSince = lastFrameMs;
    return;
  }
  finishActive(F("sent"), true);
}

void serviceTx()
{
  const unsigned long now = millis();
  if (phase == PHASE_AWAIT) {
    if (active->kind == KIND_LOCAL) {
      if (now - phaseSince >= LOCAL_REPLY_TIMEOUT_MS) finishActive(F("no answer"), false);
    } else if (now - phaseSince >= ACK_TIMEOUT_MS) {
      finishActive(F("no ack"), false);
    }
    return;
  }
  if (decoder.open() || rxTail != rxHead) return;
  const bool host = hostCount > 0;
  if (!host && !userPending) return;
  if ((long)(now - retryAt) < 0) return;
  if (!host && now - lastFrameMs < QUIET_MS) return;
  active = host ? &hostQueue[0] : &userRequest;
  sendActive(now);
}

//------------------------------------------------------------------------------
// Frames from the RM

void checkLearnedIdentity(const uint8_t *device)
{
  if (memcmp(device, settings.identity.device, 3) == 0) return;
  Serial.print(F("W the RM holds device ID "));
  printDevice(device);
  Serial.print(F(", ours is "));
  printDevice(settings.identity.device);
  Serial.println(F("; the RM takes ours at its next 41"));
}

void react(const uint8_t *d, uint8_t len, unsigned long now)
{
  if (len == 1 && d[0] == w2::MSG_IDENTITY_REQUEST) {
    if (!settings.autoReply) return;
    uint8_t reply[w2::MAX_PAYLOAD];
    const uint8_t replyLen = w2::buildIdentityReply(settings.identity, reply, sizeof(reply));
    queueHostReply(reply, replyLen, d[0]);
    return;
  }
  if (d[0] == w2::MSG_RM_STATUS && len >= 12) {
    ownSid = d[11];
    checkLearnedIdentity(&d[6]);
    return;
  }
  if (!settings.autoReply) return;
  uint8_t reply[w2::MAX_PAYLOAD];
  const uint8_t replyLen = w2::buildHostReply(d, len, settings.identity, HOST_BATTERY_MV, reply, sizeof(reply));
  if (replyLen && limiter.allow(d[0], now)) queueHostReply(reply, replyLen, d[0]);
}

void handleFrame(w2::FrameStatus status, const w2::Frame &frame)
{
  const unsigned long now = millis();
  lastFrameMs = now;
  const uint8_t *d = frame.data;
  const uint8_t len = frame.len;
  if (status != w2::FRAME_COMPLETE) {
    brokenFrames++;
    printStamp(F("RX"));
    printHexBytes(d, len);
    if (status == w2::FRAME_BAD_ESCAPE) Serial.println(F("  broken frame, bad escape"));
    else if (status == w2::FRAME_OVERFLOW) Serial.println(F("  broken frame, too long"));
    else Serial.println(F("  broken frame, no 7E"));
    return;
  }
  rxFrames++;
  if (phase == PHASE_AWAIT) {
    if (active->expectsAck && len == 1 && (d[0] == w2::MSG_ACK || d[0] == w2::MSG_REJECT)) {
      if (d[0] == w2::MSG_ACK) finishActive(F("ok (46)"), true);
      else finishActive(F("rejected (47), SID not in the local map"), false);
      return;
    }
    if (active->kind == KIND_LOCAL && d[0] == active->replyCode &&
        (active->replySub == 0 || (len > 1 && d[1] == active->replySub))) {
      finishActive(F("ok"), true);
    }
  }
  bool copy = false;
  if (w2::isRadioMessage(d, len)) copy = duplicates.seen(w2::radioSid(d, len), d[0], w2::radioSeq(d, len), now);
  printStamp(F("RX"));
  printHexBytes(d, len);
  Serial.print(F("  "));
  describe(d, len);
  if (copy) {
    copies++;
    Serial.print(F(" (copy)"));
  }
  Serial.println();
  if (!copy) react(d, len, now);
}

void serviceRx()
{
  uint8_t value;
  while (popRxByte(value)) {
    const w2::FrameStatus status = decoder.feed(value, millis());
    if (status != w2::FRAME_NONE) handleFrame(status, decoder.frame());
  }
  const w2::FrameStatus status = decoder.pollTimeout(millis());
  if (status != w2::FRAME_NONE) handleFrame(status, decoder.frame());
  if (rxDropped) {
    noInterrupts();
    const uint8_t dropped = rxDropped;
    rxDropped = 0;
    interrupts();
    lostBytes += dropped;
    Serial.print(F("E "));
    Serial.print(dropped);
    Serial.println(F(" bytes from the RM lost"));
  }
}

//------------------------------------------------------------------------------
// Serial commands

void printIdentity()
{
  Serial.print(F("I identity "));
  printDevice(settings.identity.device);
  Serial.print(F(", model "));
  Serial.print(settings.identity.model);
  Serial.print(F(", class "));
  printHex2(settings.identity.deviceClass);
  Serial.print(F(", automatic answers "));
  Serial.println(settings.autoReply ? F("on") : F("off"));
}

void printHelp()
{
  Serial.println(F("I commands, one per line:"));
  Serial.println(F("I   STATUS            D1, status of the own RM"));
  Serial.println(F("I   MAP               D3 03, local SID map"));
  Serial.println(F("I   RS <sid>          D3 06 sid 00, remote status of an RM"));
  Serial.println(F("I   IDQ <sid>         D3 06 sid 01, ID request to the host of an RM"));
  Serial.println(F("I   S <hex>           read-only request, e.g. S C7 3D E4 2F 7C 04 06"));
  Serial.println(F("I   S! <hex>          any frame; can sound detectors or change the network"));
  Serial.println(F("I   INIT!             D3 19 50 00; the RM floods it through the network"));
  Serial.println(F("I   IDENT [<id> <model> <class>]  e.g. IDENT 13B8A5 785 FF"));
  Serial.println(F("I   AUTO ON|OFF       answer 41, C1, C3, and 72/C5/C7 for our ID"));
  Serial.println(F("I   INFO              settings and counters"));
  Serial.println(F("I output: RX frame from the RM, TX own frame, AUTO automatic answer, I/W/E info/warning/error"));
}

void printInfo()
{
  Serial.print(F("I FireAngelNanoTest "));
  Serial.print(FIRMWARE_VERSION);
  Serial.print(F(", uptime "));
  Serial.print(millis());
  Serial.println(F(" ms"));
  printIdentity();
  Serial.print(F("I RM SID "));
  if (ownSid == 0xFF) Serial.println(F("unknown, STATUS asks the RM"));
  else Serial.println(ownSid);
  Serial.print(F("I frames received "));
  Serial.print(rxFrames);
  Serial.print(F(", broken "));
  Serial.print(brokenFrames);
  Serial.print(F(", copies "));
  Serial.print(copies);
  Serial.print(F(", lost bytes "));
  Serial.print(lostBytes);
  Serial.print(F("; sent ok "));
  Serial.print(txOk);
  Serial.print(F(", failed "));
  Serial.println(txFailed);
}

bool parseNumber(const char *text, unsigned long maximum, unsigned long &value)
{
  if (!text || !*text) return false;
  value = 0;
  for (; *text; text++) {
    if (*text < '0' || *text > '9') return false;
    value = value * 10 + (*text - '0');
    if (value > maximum) return false;
  }
  return true;
}

void commandRemoteStatus(char *args, uint8_t kind)
{
  unsigned long sid;
  if (!parseNumber(strtok(args, " "), w2::MAX_SID, sid)) {
    Serial.println(F("E SID 0-63 expected"));
    return;
  }
  const uint8_t request[] = {w2::MSG_RM_COMMAND, 0x06, (uint8_t)sid, kind};
  submit(request, sizeof(request));
}

void commandSend(char *args, bool force)
{
  uint8_t data[w2::MAX_PAYLOAD];
  const uint8_t len = args ? w2::parseHexBytes(args, data, sizeof(data)) : 0;
  if (!len) {
    Serial.println(F("E hex bytes expected, at most 16"));
    return;
  }
  if (!force && !w2::isReadOnlyRequest(data, len)) {
    Serial.println(F("E not a read-only request; S! sends it anyway"));
    return;
  }
  submit(data, len);
}

void commandIdent(char *args)
{
  char *idText = strtok(args, " ");
  if (!idText) {
    printIdentity();
    return;
  }
  char *modelText = strtok(NULL, " ");
  char *classText = strtok(NULL, " ");
  w2::Identity identity;
  unsigned long model;
  uint8_t deviceClass;
  if (!w2::parseDeviceId(idText, identity.device) || !parseNumber(modelText, 65535UL, model) || !classText ||
      strlen(classText) != 2 || w2::parseHexBytes(classText, &deviceClass, 1) != 1) {
    Serial.println(F("E IDENT <id> <model> <class> expected, e.g. IDENT 13B8A5 785 FF"));
    return;
  }
  identity.model = (uint16_t)model;
  identity.deviceClass = deviceClass;
  settings.identity = identity;
  saveSettings();
  printIdentity();
  Serial.println(F("I saved; the RM takes it at its next 41"));
}

void commandAuto(char *args)
{
  char *mode = strtok(args, " ");
  if (mode && !strcmp(mode, "ON")) settings.autoReply = 1;
  else if (mode && !strcmp(mode, "OFF")) settings.autoReply = 0;
  else {
    Serial.println(F("E AUTO ON or AUTO OFF expected"));
    return;
  }
  saveSettings();
  printIdentity();
}

void handleLine(char *line)
{
  for (char *p = line; *p; p++) *p = toupper(*p);
  char *command = strtok(line, " ");
  if (!command) return;
  char *args = strtok(NULL, "");
  if (!strcmp(command, "HELP") || !strcmp(command, "?")) {
    printHelp();
  } else if (!strcmp(command, "INFO")) {
    printInfo();
  } else if (!strcmp(command, "STATUS")) {
    const uint8_t request[] = {w2::MSG_RM_STATUS_REQUEST};
    submit(request, sizeof(request));
  } else if (!strcmp(command, "MAP")) {
    const uint8_t request[] = {w2::MSG_RM_COMMAND, 0x03};
    submit(request, sizeof(request));
  } else if (!strcmp(command, "RS")) {
    commandRemoteStatus(args, 0x00);
  } else if (!strcmp(command, "IDQ")) {
    commandRemoteStatus(args, 0x01);
  } else if (!strcmp(command, "S")) {
    commandSend(args, false);
  } else if (!strcmp(command, "S!")) {
    commandSend(args, true);
  } else if (!strcmp(command, "INIT!")) {
    const uint8_t request[] = {w2::MSG_RM_COMMAND, 0x19, 0x50, 0x00};
    submit(request, sizeof(request));
  } else if (!strcmp(command, "IDENT")) {
    commandIdent(args);
  } else if (!strcmp(command, "AUTO")) {
    commandAuto(args);
  } else {
    Serial.println(F("E unknown command, HELP lists them"));
  }
}

char line[LINE_MAX + 1];
uint8_t lineLen = 0;
bool lineOverflow = false;

void serviceSerial()
{
  while (Serial.available() > 0) {
    const char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (lineOverflow) Serial.println(F("E line too long"));
      else if (lineLen) {
        line[lineLen] = '\0';
        handleLine(line);
      }
      lineLen = 0;
      lineOverflow = false;
    } else if (lineLen < LINE_MAX) {
      line[lineLen++] = c;
    } else {
      lineOverflow = true;
    }
  }
}

//------------------------------------------------------------------------------

void setup()
{
  // IRQ high asks the RM to clock a byte from us; keep it low from the start.
  PORTB &= ~IRQ_BIT;
  DDRB |= IRQ_BIT;
  MCUSR = 0;
  wdt_disable();
  setupSpiSlave();
  loadSettings();
  Serial.begin(115200);
  Serial.print(F("I FireAngelNanoTest "));
  Serial.print(FIRMWARE_VERSION);
  Serial.println(F(" ready; nothing is sent at startup, HELP lists the commands"));
  printIdentity();
}

void loop()
{
  serviceRx();
  serviceSerial();
  serviceTx();
}
