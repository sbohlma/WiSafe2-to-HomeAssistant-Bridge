#ifndef W2_PROTOCOL_H
#define W2_PROTOCOL_H

// Radio-module (RM) protocol logic without Arduino dependencies, so it can be
// compiled and tested on a PC (see tests/test_w2_protocol.cpp).

#include <stddef.h>
#include <stdint.h>

namespace w2 {

const uint8_t FRAME_END = 0x7E;
const uint8_t FRAME_ESCAPE = 0x7D;
const uint8_t MAX_FRAME = 32;
// Unescaped payload of a frame sent to the RM, without the final 7E.
const uint8_t MAX_PAYLOAD = 16;
// Escaped payload plus 7E.
const uint8_t MAX_ENCODED = 2 * MAX_PAYLOAD + 1;
const uint16_t FRAME_TIMEOUT_MS = 100;

const uint8_t MSG_ACK = 0x46;
const uint8_t MSG_REJECT = 0x47;
const uint8_t MSG_IDENTITY_REQUEST = 0x41;
const uint8_t MSG_IDENTITY_REPLY = 0x91;
const uint8_t MSG_RM_STATUS_REQUEST = 0xD1;
const uint8_t MSG_RM_STATUS = 0xD2;
const uint8_t MSG_RM_COMMAND = 0xD3;
const uint8_t MSG_RM_REPORT = 0xD4;
const uint8_t SID_UNPAIRED = 0x40;
const uint8_t MAX_SID = 63;

enum FrameStatus : uint8_t {
  FRAME_NONE,
  FRAME_COMPLETE,
  FRAME_BAD_ESCAPE,  // 7D followed by something other than 01/02
  FRAME_OVERFLOW,    // longer than MAX_FRAME; the rest up to 7E is discarded
  FRAME_TIMEOUT,     // no 7E within FRAME_TIMEOUT_MS
};

struct Frame {
  uint8_t len;
  uint8_t data[MAX_FRAME];
};

// Splits the byte stream from the RM into unescaped frames. 7E inside a frame
// arrives as 7D 01, 7D as 7D 02. 00 before a frame is filler.
class FrameDecoder {
 public:
  // A status other than FRAME_NONE means frame() holds a finished frame until
  // the next call.
  FrameStatus feed(uint8_t byte, uint32_t nowMs);
  FrameStatus pollTimeout(uint32_t nowMs);
  bool open() const { return !done_ && (frame_.len > 0 || escape_); }
  const Frame &frame() const { return frame_; }

 private:
  FrameStatus finish_(FrameStatus status);
  bool push_(uint8_t byte);

  Frame frame_{};
  bool done_{false};
  bool escape_{false};
  bool badEscape_{false};
  bool discarding_{false};
  uint32_t lastByteMs_{0};
};

// Escapes the payload and appends 7E. Returns the encoded length, 0 if out is
// too small.
uint8_t encodeFrame(const uint8_t *payload, uint8_t len, uint8_t *out, uint8_t cap);

// Messages from the network carry the sender's SID and a sequence number as the
// last two bytes. Minimum length including both; 0 for local frames and
// unknown types.
uint8_t radioMinLength(uint8_t code);
bool isRadioMessage(const uint8_t *data, uint8_t len);
inline uint8_t radioSid(const uint8_t *data, uint8_t len) { return data[len - 2]; }
inline uint8_t radioSeq(const uint8_t *data, uint8_t len) { return data[len - 1]; }

struct Identity {
  uint8_t device[3];  // packet order: A5 B8 13 is shown as "13B8A5"
  uint16_t model;     // 785 = WST-630, sent as 11 03
  uint8_t deviceClass;
};

// 91 dev(3) model(2) class 05 01 01, the answer to 41.
uint8_t buildIdentityReply(const Identity &identity, uint8_t *out, uint8_t cap);

// Answers to requests the RM hands to its host: C3 -> C4, C1 -> C2, and for
// requests carrying our own device ID 72 -> 73, C7 -> C8, C5 -> C6. Returns 0
// if the frame needs no answer from us.
uint8_t buildHostReply(const uint8_t *request, uint8_t len, const Identity &identity, uint16_t batteryMv,
                       uint8_t *out, uint8_t cap);

// Remembers (SID, type, sequence number) for WINDOW_MS; seen() is true for a
// copy of a message already handled.
class DuplicateFilter {
 public:
  static const uint32_t WINDOW_MS = 60000;
  bool seen(uint8_t sid, uint8_t code, uint8_t seq, uint32_t nowMs);

 private:
  static const uint8_t ENTRIES = 8;
  struct Entry {
    bool used;
    uint8_t sid;
    uint8_t code;
    uint8_t seq;
    uint32_t atMs;
  };
  Entry entries_[ENTRIES]{};
  uint8_t next_{0};
};

// At most one answer per request type within WINDOW_MS.
class ReplyLimiter {
 public:
  static const uint32_t WINDOW_MS = 60000;
  bool allow(uint8_t code, uint32_t nowMs);

 private:
  static int8_t slot_(uint8_t code);
  bool used_[5]{};
  uint32_t lastMs_[5]{};
};

// Requests that only read: D1, D3 03, D3 06 sid 00|01, 72/C5/C7 dev(3)
// model(2) x. Everything else can sound detectors or change the network.
bool isReadOnlyRequest(const uint8_t *data, uint8_t len);

// "D3 06 1A 00" or "D3061A00". Number of bytes, 0 if invalid, empty or longer
// than cap.
uint8_t parseHexBytes(const char *text, uint8_t *out, uint8_t cap);
// Display order to packet order: "13B8A5" gives A5 B8 13.
bool parseDeviceId(const char *text, uint8_t *device);
// RM battery from BVNL: U = 1048.576 / (BVNL + 285) V.
uint16_t rmBatteryMv(uint8_t bvnl);

}  // namespace w2

#endif
