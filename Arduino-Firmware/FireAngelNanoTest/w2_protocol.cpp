#include "w2_protocol.h"

#include <string.h>

namespace w2 {

FrameStatus FrameDecoder::finish_(FrameStatus status) {
  done_ = true;
  escape_ = false;
  badEscape_ = false;
  return status;
}

bool FrameDecoder::push_(uint8_t byte) {
  if (frame_.len >= MAX_FRAME) return false;
  frame_.data[frame_.len++] = byte;
  return true;
}

FrameStatus FrameDecoder::feed(uint8_t byte, uint32_t nowMs) {
  lastByteMs_ = nowMs;
  if (done_) {
    done_ = false;
    frame_.len = 0;
  }
  if (discarding_) {
    if (byte == FRAME_END) discarding_ = false;
    return FRAME_NONE;
  }
  if (byte == FRAME_END) {
    if (!open()) return FRAME_NONE;
    if (escape_) {
      badEscape_ = true;
      push_(FRAME_ESCAPE);
    }
    return finish_(badEscape_ ? FRAME_BAD_ESCAPE : FRAME_COMPLETE);
  }
  if (byte == 0x00 && !open()) return FRAME_NONE;

  bool stored;
  if (escape_) {
    escape_ = false;
    if (byte == 0x01) {
      stored = push_(FRAME_END);
    } else if (byte == 0x02) {
      stored = push_(FRAME_ESCAPE);
    } else {
      badEscape_ = true;
      stored = push_(FRAME_ESCAPE) && push_(byte);
    }
  } else if (byte == FRAME_ESCAPE) {
    escape_ = true;
    return FRAME_NONE;
  } else {
    stored = push_(byte);
  }
  if (stored) return FRAME_NONE;
  discarding_ = true;
  return finish_(FRAME_OVERFLOW);
}

FrameStatus FrameDecoder::pollTimeout(uint32_t nowMs) {
  if (!open() || nowMs - lastByteMs_ < FRAME_TIMEOUT_MS) return FRAME_NONE;
  if (escape_) push_(FRAME_ESCAPE);
  return finish_(FRAME_TIMEOUT);
}

uint8_t encodeFrame(const uint8_t *payload, uint8_t len, uint8_t *out, uint8_t cap) {
  uint8_t used = 0;
  for (uint8_t i = 0; i < len; i++) {
    const uint8_t byte = payload[i];
    const bool masked = byte == FRAME_END || byte == FRAME_ESCAPE;
    if (used + (masked ? 2 : 1) > cap) return 0;
    if (masked) {
      out[used++] = FRAME_ESCAPE;
      out[used++] = byte == FRAME_END ? 0x01 : 0x02;
    } else {
      out[used++] = byte;
    }
  }
  if (used + 1 > cap) return 0;
  out[used++] = FRAME_END;
  return used;
}

uint8_t radioMinLength(uint8_t code) {
  switch (code) {
    case 0x51: return 7;
    case 0x50:
    case 0x61: return 8;
    case 0x71:
    case 0x72:
    case 0xC4:
    case 0xC5:
    case 0xC7:
    case 0xD5: return 9;
    case 0x70:
    case 0x73:
    case 0xC1:
    case 0xC2: return 10;
    case 0xC6:
    case 0xC8: return 11;
    case 0xC3: return 6;
    default: return 0;
  }
}

bool isRadioMessage(const uint8_t *data, uint8_t len) {
  if (len == 0) return false;
  const uint8_t minimum = radioMinLength(data[0]);
  return minimum != 0 && len >= minimum;
}

namespace {

class Writer {
 public:
  Writer(uint8_t *out, uint8_t cap) : out_(out), cap_(cap) {}
  void byte(uint8_t value) {
    if (len_ < cap_) out_[len_] = value;
    len_++;
  }
  void bytes(const uint8_t *values, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) byte(values[i]);
  }
  void model(uint16_t model) {
    byte(model & 0xFF);
    byte(model >> 8);
  }
  uint8_t finish() const { return len_ <= cap_ ? len_ : 0; }

 private:
  uint8_t *out_;
  uint8_t cap_;
  uint8_t len_{0};
};

bool isOwnDevice(const uint8_t *request, const Identity &identity) {
  return memcmp(&request[1], identity.device, 3) == 0;
}

}  // namespace

uint8_t buildIdentityReply(const Identity &identity, uint8_t *out, uint8_t cap) {
  Writer writer(out, cap);
  writer.byte(MSG_IDENTITY_REPLY);
  writer.bytes(identity.device, 3);
  writer.model(identity.model);
  writer.byte(identity.deviceClass);
  writer.byte(0x05);
  writer.byte(0x01);
  writer.byte(0x01);
  return writer.finish();
}

uint8_t buildHostReply(const uint8_t *request, uint8_t len, const Identity &identity, uint16_t batteryMv,
                       uint8_t *out, uint8_t cap) {
  if (!isRadioMessage(request, len)) return 0;
  Writer writer(out, cap);
  switch (request[0]) {
    case 0xC3:
      writer.byte(0xC4);
      writer.bytes(identity.device, 3);
      writer.byte(identity.deviceClass);
      writer.model(identity.model);
      break;
    case 0xC1:
      writer.byte(0xC2);
      writer.bytes(identity.device, 3);
      writer.byte(identity.deviceClass);
      writer.model(identity.model);
      writer.byte(0x01);
      break;
    case 0x72:
      if (!isOwnDevice(request, identity)) return 0;
      writer.byte(0x73);
      writer.bytes(identity.device, 3);
      writer.model(identity.model);
      writer.byte(0x00);
      writer.byte(0x00);
      break;
    case 0xC7:
      if (!isOwnDevice(request, identity)) return 0;
      writer.byte(0xC8);
      writer.bytes(identity.device, 3);
      // Echo of d6: 06, from the WHM-1 05.
      writer.byte(request[6]);
      writer.byte(batteryMv & 0xFF);
      writer.byte(batteryMv >> 8);
      writer.byte(0x00);
      writer.byte(0x00);
      break;
    case 0xC5:
      if (!isOwnDevice(request, identity)) return 0;
      writer.byte(0xC6);
      writer.bytes(identity.device, 3);
      writer.byte(0x74);
      writer.byte(0x00);
      writer.byte(0x00);
      writer.byte(0x00);
      writer.byte(0x00);
      break;
    default:
      return 0;
  }
  return writer.finish();
}

bool DuplicateFilter::seen(uint8_t sid, uint8_t code, uint8_t seq, uint32_t nowMs) {
  for (uint8_t i = 0; i < ENTRIES; i++) {
    Entry &entry = entries_[i];
    if (!entry.used || nowMs - entry.atMs >= WINDOW_MS) {
      entry.used = false;
      continue;
    }
    if (entry.sid == sid && entry.code == code && entry.seq == seq) return true;
  }
  entries_[next_] = Entry{true, sid, code, seq, nowMs};
  next_ = (next_ + 1) % ENTRIES;
  return false;
}

int8_t ReplyLimiter::slot_(uint8_t code) {
  switch (code) {
    case 0x72: return 0;
    case 0xC1: return 1;
    case 0xC3: return 2;
    case 0xC5: return 3;
    case 0xC7: return 4;
    default: return -1;
  }
}

bool ReplyLimiter::allow(uint8_t code, uint32_t nowMs) {
  const int8_t slot = slot_(code);
  if (slot < 0) return true;
  if (used_[slot] && nowMs - lastMs_[slot] < WINDOW_MS) return false;
  used_[slot] = true;
  lastMs_[slot] = nowMs;
  return true;
}

bool isReadOnlyRequest(const uint8_t *data, uint8_t len) {
  if (len == 1) return data[0] == MSG_RM_STATUS_REQUEST;
  if (len == 2) return data[0] == MSG_RM_COMMAND && data[1] == 0x03;
  if (len == 4) {
    return data[0] == MSG_RM_COMMAND && data[1] == 0x06 && data[2] <= MAX_SID && data[3] <= 0x01;
  }
  if (len == 7) return data[0] == 0x72 || data[0] == 0xC5 || data[0] == 0xC7;
  return false;
}

namespace {

int8_t hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

}  // namespace

uint8_t parseHexBytes(const char *text, uint8_t *out, uint8_t cap) {
  uint8_t count = 0;
  while (*text) {
    if (*text == ' ') {
      text++;
      continue;
    }
    const int8_t high = hexValue(text[0]);
    const int8_t low = high < 0 ? -1 : hexValue(text[1]);
    if (low < 0 || count >= cap) return 0;
    out[count++] = (high << 4) | low;
    text += 2;
  }
  return count;
}

bool parseDeviceId(const char *text, uint8_t *device) {
  uint8_t display[3];
  if (strlen(text) != 6 || parseHexBytes(text, display, sizeof(display)) != 3) return false;
  device[0] = display[2];
  device[1] = display[1];
  device[2] = display[0];
  return true;
}

uint16_t rmBatteryMv(uint8_t bvnl) { return 1048576UL / (bvnl + 285U); }

}  // namespace w2
