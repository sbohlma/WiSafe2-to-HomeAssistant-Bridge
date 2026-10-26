// Host tests for w2_protocol, e.g.:
//   c++ -std=c++11 -I.. ../w2_protocol.cpp test_w2_protocol.cpp -o test_w2_protocol && ./test_w2_protocol
// The Arduino builder only compiles the sketch folder and src/, not tests/.

#include "w2_protocol.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool condition, const char *what, int line) {
  if (condition) return;
  std::printf("FAIL line %d: %s\n", line, what);
  failures++;
}

#define CHECK(condition) check((condition), #condition, __LINE__)

bool equal(const uint8_t *data, uint8_t len, const uint8_t *expected, uint8_t expectedLen) {
  return len == expectedLen && std::memcmp(data, expected, len) == 0;
}

w2::FrameStatus feedAll(w2::FrameDecoder &decoder, const uint8_t *bytes, size_t len, uint32_t nowMs) {
  w2::FrameStatus status = w2::FRAME_NONE;
  for (size_t i = 0; i < len; i++) {
    const w2::FrameStatus result = decoder.feed(bytes[i], nowMs);
    if (result != w2::FRAME_NONE) status = result;
  }
  return status;
}

const w2::Identity IDENTITY = {{0xA5, 0xB8, 0x13}, 785, 0xFF};

void testDecoder() {
  w2::FrameDecoder decoder;
  const uint8_t ack[] = {0x00, 0x00, 0x46, 0x7E};
  CHECK(feedAll(decoder, ack, sizeof(ack), 0) == w2::FRAME_COMPLETE);
  const uint8_t ackFrame[] = {0x46};
  CHECK(equal(decoder.frame().data, decoder.frame().len, ackFrame, 1));
  CHECK(!decoder.open());

  // 72 dev model 00 7E sid seq, the 7E escaped as 7D 01; 00 inside a frame is data.
  const uint8_t escaped[] = {0x72, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0x00, 0x7D, 0x01, 0x0F, 0x0F, 0x7E};
  const uint8_t plain[] = {0x72, 0xA5, 0xB8, 0x14, 0x11, 0x03, 0x00, 0x7E, 0x0F, 0x0F};
  CHECK(feedAll(decoder, escaped, sizeof(escaped), 0) == w2::FRAME_COMPLETE);
  CHECK(equal(decoder.frame().data, decoder.frame().len, plain, sizeof(plain)));

  const uint8_t escapedEscape[] = {0xC4, 0x7D, 0x02, 0x7E};
  const uint8_t plainEscape[] = {0xC4, 0x7D};
  CHECK(feedAll(decoder, escapedEscape, sizeof(escapedEscape), 0) == w2::FRAME_COMPLETE);
  CHECK(equal(decoder.frame().data, decoder.frame().len, plainEscape, sizeof(plainEscape)));

  const uint8_t badEscape[] = {0xC4, 0x7D, 0x05, 0x7E};
  CHECK(feedAll(decoder, badEscape, sizeof(badEscape), 0) == w2::FRAME_BAD_ESCAPE);

  const uint8_t lone7E[] = {0x7E, 0x7E};
  CHECK(feedAll(decoder, lone7E, sizeof(lone7E), 0) == w2::FRAME_NONE);

  // Too long: overflow once, the rest up to 7E is dropped, the next frame is fine.
  uint8_t longFrame[w2::MAX_FRAME + 1];
  std::memset(longFrame, 0x11, sizeof(longFrame));
  CHECK(feedAll(decoder, longFrame, sizeof(longFrame), 0) == w2::FRAME_OVERFLOW);
  CHECK(decoder.frame().len == w2::MAX_FRAME);
  const uint8_t tail[] = {0x11, 0x22, 0x7E, 0x47, 0x7E};
  CHECK(feedAll(decoder, tail, sizeof(tail), 0) == w2::FRAME_COMPLETE);
  const uint8_t rejectFrame[] = {0x47};
  CHECK(equal(decoder.frame().data, decoder.frame().len, rejectFrame, 1));

  // A frame of exactly MAX_FRAME bytes still fits.
  uint8_t fullFrame[w2::MAX_FRAME + 1];
  std::memset(fullFrame, 0x33, w2::MAX_FRAME);
  fullFrame[w2::MAX_FRAME] = 0x7E;
  CHECK(feedAll(decoder, fullFrame, sizeof(fullFrame), 0) == w2::FRAME_COMPLETE);
  CHECK(decoder.frame().len == w2::MAX_FRAME);

  // Timeout without 7E.
  const uint8_t cut[] = {0xD2, 0x20};
  CHECK(feedAll(decoder, cut, sizeof(cut), 1000) == w2::FRAME_NONE);
  CHECK(decoder.open());
  CHECK(decoder.pollTimeout(1000 + w2::FRAME_TIMEOUT_MS - 1) == w2::FRAME_NONE);
  CHECK(decoder.pollTimeout(1000 + w2::FRAME_TIMEOUT_MS) == w2::FRAME_TIMEOUT);
  CHECK(decoder.frame().len == 2);
  CHECK(!decoder.open());
  CHECK(decoder.pollTimeout(5000) == w2::FRAME_NONE);
}

void testEncode() {
  const uint8_t payload[] = {0x72, 0x7E, 0x7D, 0x7F};
  const uint8_t expected[] = {0x72, 0x7D, 0x01, 0x7D, 0x02, 0x7F, 0x7E};
  uint8_t out[w2::MAX_ENCODED];
  const uint8_t len = w2::encodeFrame(payload, sizeof(payload), out, sizeof(out));
  CHECK(equal(out, len, expected, sizeof(expected)));
  CHECK(w2::encodeFrame(payload, sizeof(payload), out, 6) == 0);

  uint8_t worst[w2::MAX_PAYLOAD];
  std::memset(worst, 0x7E, sizeof(worst));
  CHECK(w2::encodeFrame(worst, sizeof(worst), out, sizeof(out)) == w2::MAX_ENCODED);
}

void testRadio() {
  const uint8_t test[] = {0x70, 0x3D, 0xE4, 0x2F, 0x81, 0x01, 0x7C, 0x04, 0x0E, 0x03};
  CHECK(w2::isRadioMessage(test, sizeof(test)));
  CHECK(w2::radioSid(test, sizeof(test)) == 14);
  CHECK(w2::radioSeq(test, sizeof(test)) == 3);
  CHECK(!w2::isRadioMessage(test, sizeof(test) - 1));

  const uint8_t idRequest[] = {0xC3, 0x00, 0x00, 0x00, 0x0F, 0x0B};
  CHECK(w2::isRadioMessage(idRequest, sizeof(idRequest)));
  CHECK(w2::radioSid(idRequest, sizeof(idRequest)) == 15);

  const uint8_t status[] = {0xD2, 0x2B, 0x2E, 0x41, 0x65, 0xDF, 0xA5, 0xB8, 0x13, 0x00, 0x01, 0x10, 0x00};
  CHECK(!w2::isRadioMessage(status, sizeof(status)));
  const uint8_t ack[] = {0x46};
  CHECK(!w2::isRadioMessage(ack, 1));
  const uint8_t text[] = {0x3E, 0x54, 0x20, 0x31, 0x34, 0x14, 0x02};
  CHECK(!w2::isRadioMessage(text, sizeof(text)));
}

void testIdentityReply() {
  uint8_t out[w2::MAX_PAYLOAD];
  const uint8_t expected[] = {0x91, 0xA5, 0xB8, 0x13, 0x11, 0x03, 0xFF, 0x05, 0x01, 0x01};
  const uint8_t len = w2::buildIdentityReply(IDENTITY, out, sizeof(out));
  CHECK(equal(out, len, expected, sizeof(expected)));
  CHECK(w2::buildIdentityReply(IDENTITY, out, 9) == 0);
}

void testHostReplies() {
  uint8_t out[w2::MAX_PAYLOAD];
  uint8_t len;

  const uint8_t idRequest[] = {0xC3, 0x00, 0x00, 0x00, 0x0F, 0x0B};
  const uint8_t idInfo[] = {0xC4, 0xA5, 0xB8, 0x13, 0xFF, 0x11, 0x03};
  len = w2::buildHostReply(idRequest, sizeof(idRequest), IDENTITY, 3000, out, sizeof(out));
  CHECK(equal(out, len, idInfo, sizeof(idInfo)));

  const uint8_t diagnose[] = {0xC1, 0, 0, 0, 0, 0, 0, 0x01, 0x0F, 0x0D};
  const uint8_t diagnoseResult[] = {0xC2, 0xA5, 0xB8, 0x13, 0xFF, 0x11, 0x03, 0x01};
  len = w2::buildHostReply(diagnose, sizeof(diagnose), IDENTITY, 3000, out, sizeof(out));
  CHECK(equal(out, len, diagnoseResult, sizeof(diagnoseResult)));

  // Requests from the W2-MM carry an extra 7E before SID and sequence number.
  const uint8_t battery[] = {0xC7, 0xA5, 0xB8, 0x13, 0x11, 0x03, 0x06, 0x7E, 0x0F, 0x0D};
  const uint8_t batteryReply[] = {0xC8, 0xA5, 0xB8, 0x13, 0x06, 0xB8, 0x0B, 0x00, 0x00};
  len = w2::buildHostReply(battery, sizeof(battery), IDENTITY, 3000, out, sizeof(out));
  CHECK(equal(out, len, batteryReply, sizeof(batteryReply)));

  const uint8_t batteryWhm[] = {0xC7, 0xA5, 0xB8, 0x13, 0x11, 0x03, 0x05, 0x0F, 0x0D};
  len = w2::buildHostReply(batteryWhm, sizeof(batteryWhm), IDENTITY, 3000, out, sizeof(out));
  CHECK(len == sizeof(batteryReply) && out[4] == 0x05);

  const uint8_t details[] = {0x72, 0xA5, 0xB8, 0x13, 0x11, 0x03, 0x00, 0x7E, 0x0F, 0x0F};
  const uint8_t detailsReply[] = {0x73, 0xA5, 0xB8, 0x13, 0x11, 0x03, 0x00, 0x00};
  len = w2::buildHostReply(details, sizeof(details), IDENTITY, 3000, out, sizeof(out));
  CHECK(equal(out, len, detailsReply, sizeof(detailsReply)));

  const uint8_t events[] = {0xC5, 0xA5, 0xB8, 0x13, 0x11, 0x03, 0x74, 0x7E, 0x0F, 0x0E};
  const uint8_t eventsReply[] = {0xC6, 0xA5, 0xB8, 0x13, 0x74, 0x00, 0x00, 0x00, 0x00};
  len = w2::buildHostReply(events, sizeof(events), IDENTITY, 3000, out, sizeof(out));
  CHECK(equal(out, len, eventsReply, sizeof(eventsReply)));

  // Device-addressed requests for another device are flooded to every host.
  const uint8_t foreign[] = {0xC7, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x06, 0x14, 0x05};
  CHECK(w2::buildHostReply(foreign, sizeof(foreign), IDENTITY, 3000, out, sizeof(out)) == 0);
  const uint8_t foreignDetails[] = {0x72, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x00, 0x14, 0x05};
  CHECK(w2::buildHostReply(foreignDetails, sizeof(foreignDetails), IDENTITY, 3000, out, sizeof(out)) == 0);

  // Messages that are no requests, and truncated requests.
  const uint8_t test[] = {0x70, 0x3D, 0xE4, 0x2F, 0x81, 0x01, 0x7C, 0x04, 0x0E, 0x03};
  CHECK(w2::buildHostReply(test, sizeof(test), IDENTITY, 3000, out, sizeof(out)) == 0);
  CHECK(w2::buildHostReply(idRequest, sizeof(idRequest) - 1, IDENTITY, 3000, out, sizeof(out)) == 0);
  CHECK(w2::buildHostReply(idRequest, sizeof(idRequest), IDENTITY, 3000, out, 6) == 0);
}

void testDuplicates() {
  w2::DuplicateFilter filter;
  CHECK(!filter.seen(11, 0x73, 12, 1000));
  CHECK(filter.seen(11, 0x73, 12, 1001));
  CHECK(!filter.seen(11, 0x73, 13, 1002));
  CHECK(!filter.seen(12, 0x73, 12, 1003));
  CHECK(!filter.seen(11, 0xC8, 12, 1004));
  CHECK(!filter.seen(11, 0x73, 12, 1000 + w2::DuplicateFilter::WINDOW_MS));
  // millis() rollover.
  CHECK(!filter.seen(5, 0x70, 1, 0xFFFFFF00UL));
  CHECK(filter.seen(5, 0x70, 1, 0x00000100UL));
}

void testLimiter() {
  w2::ReplyLimiter limiter;
  CHECK(limiter.allow(0xC3, 0));
  CHECK(!limiter.allow(0xC3, w2::ReplyLimiter::WINDOW_MS - 1));
  CHECK(limiter.allow(0xC7, 10));
  CHECK(limiter.allow(0xC3, w2::ReplyLimiter::WINDOW_MS));
  CHECK(limiter.allow(0x41, 0));
  CHECK(limiter.allow(0x41, 1));
}

void testReadOnly() {
  const uint8_t d1[] = {0xD1};
  const uint8_t map[] = {0xD3, 0x03};
  const uint8_t remoteStatus[] = {0xD3, 0x06, 0x1A, 0x00};
  const uint8_t idQuery[] = {0xD3, 0x06, 0x1A, 0x01};
  const uint8_t idQueryOther[] = {0xD3, 0x06, 0x1A, 0x02};
  const uint8_t badSid[] = {0xD3, 0x06, 0x40, 0x00};
  const uint8_t battery[] = {0xC7, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x06};
  const uint8_t details[] = {0x72, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x00};
  const uint8_t events[] = {0xC5, 0x3D, 0xE4, 0x2F, 0x7C, 0x04, 0x74};
  CHECK(w2::isReadOnlyRequest(d1, sizeof(d1)));
  CHECK(w2::isReadOnlyRequest(map, sizeof(map)));
  CHECK(w2::isReadOnlyRequest(remoteStatus, sizeof(remoteStatus)));
  CHECK(w2::isReadOnlyRequest(idQuery, sizeof(idQuery)));
  CHECK(!w2::isReadOnlyRequest(idQueryOther, sizeof(idQueryOther)));
  CHECK(!w2::isReadOnlyRequest(badSid, sizeof(badSid)));
  CHECK(w2::isReadOnlyRequest(battery, sizeof(battery)));
  CHECK(w2::isReadOnlyRequest(details, sizeof(details)));
  CHECK(w2::isReadOnlyRequest(events, sizeof(events)));

  const uint8_t init[] = {0xD3, 0x19, 0x50, 0x00};
  const uint8_t pairing[] = {0xD3, 0x12, 0x01};
  const uint8_t test[] = {0x70, 0xA5, 0xB8, 0x13, 0xFF, 0x01, 0x11, 0x03};
  const uint8_t alarm[] = {0x50, 0xA5, 0xB8, 0x13, 0x81, 0x00};
  const uint8_t silence[] = {0x61, 0xA5, 0xB8, 0x13, 0x80, 0x01};
  const uint8_t stop[] = {0x51, 0xA5, 0xB8, 0x13, 0x81};
  const uint8_t identity[] = {0x91, 0xA5, 0xB8, 0x13, 0x11, 0x03, 0xFF, 0x05, 0x01, 0x01};
  const uint8_t text[] = {0x3E, 0x54, 0x20, 0x31, 0x41};
  const uint8_t d1Long[] = {0xD1, 0x00};
  const uint8_t batteryShort[] = {0xC7, 0x3D, 0xE4, 0x2F, 0x7C, 0x04};
  CHECK(!w2::isReadOnlyRequest(init, sizeof(init)));
  CHECK(!w2::isReadOnlyRequest(pairing, sizeof(pairing)));
  CHECK(!w2::isReadOnlyRequest(test, sizeof(test)));
  CHECK(!w2::isReadOnlyRequest(alarm, sizeof(alarm)));
  CHECK(!w2::isReadOnlyRequest(silence, sizeof(silence)));
  CHECK(!w2::isReadOnlyRequest(stop, sizeof(stop)));
  CHECK(!w2::isReadOnlyRequest(identity, sizeof(identity)));
  CHECK(!w2::isReadOnlyRequest(text, sizeof(text)));
  CHECK(!w2::isReadOnlyRequest(d1Long, sizeof(d1Long)));
  CHECK(!w2::isReadOnlyRequest(batteryShort, sizeof(batteryShort)));
}

void testParsing() {
  uint8_t out[4];
  const uint8_t expected[] = {0xD3, 0x06, 0x1A, 0x00};
  CHECK(equal(out, w2::parseHexBytes("D3 06 1A 00", out, sizeof(out)), expected, 4));
  CHECK(equal(out, w2::parseHexBytes("d3061a00", out, sizeof(out)), expected, 4));
  CHECK(equal(out, w2::parseHexBytes("  D306 1A00 ", out, sizeof(out)), expected, 4));
  CHECK(w2::parseHexBytes("", out, sizeof(out)) == 0);
  CHECK(w2::parseHexBytes("   ", out, sizeof(out)) == 0);
  CHECK(w2::parseHexBytes("D3 0", out, sizeof(out)) == 0);
  CHECK(w2::parseHexBytes("D3 0G", out, sizeof(out)) == 0);
  CHECK(w2::parseHexBytes("D3 06 1A 00 01", out, sizeof(out)) == 0);

  uint8_t device[3];
  const uint8_t packet[] = {0xA5, 0xB8, 0x13};
  CHECK(w2::parseDeviceId("13B8A5", device) && std::memcmp(device, packet, 3) == 0);
  CHECK(!w2::parseDeviceId("13B8A", device));
  CHECK(!w2::parseDeviceId("13B8A5F", device));
  CHECK(!w2::parseDeviceId("13 B8 A5", device));
  CHECK(!w2::parseDeviceId("13B8AZ", device));
}

void testBattery() {
  CHECK(w2::rmBatteryMv(0x3E) == 3021);
  CHECK(w2::rmBatteryMv(0x86) == 2502);
  CHECK(w2::rmBatteryMv(0x20) == 3307);
}

}  // namespace

int main() {
  testDecoder();
  testEncode();
  testRadio();
  testIdentityReply();
  testHostReplies();
  testDuplicates();
  testLimiter();
  testReadOnly();
  testParsing();
  testBattery();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
