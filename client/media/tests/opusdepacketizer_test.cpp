// Unit test for the own Opus RTP depacketizer (synthetic RTP packets, no network).
#include <QtTest>

#include "opusdepacketizer.h"

using namespace framebeam;

namespace {
std::vector<std::byte> packet(uint8_t first, uint16_t seq, uint32_t ts, const std::vector<uint8_t> &afterFixed) {
  std::vector<uint8_t> b = {first, uint8_t(0x80 | 111), uint8_t(seq >> 8), uint8_t(seq & 0xFF),
                            uint8_t(ts >> 24), uint8_t(ts >> 16), uint8_t(ts >> 8), uint8_t(ts),
                            0xDE, 0xAD, 0xBE, 0xEF};
  b.insert(b.end(), afterFixed.begin(), afterFixed.end());
  std::vector<std::byte> out;
  for (auto c : b) out.push_back(std::byte(c));
  return out;
}
}  // namespace

class OpusDepacketizerTest : public QObject {
  Q_OBJECT
private slots:
  void plainPacket() {
    const auto p = packet(0x80, 0x1234, 960000, {1, 2, 3});
    const auto v = parseRtpPacket(p);
    QVERIFY(v);
    QCOMPARE(v->payloadOffset, size_t(12));
    QCOMPARE(v->payloadSize, size_t(3));
    QCOMPARE(v->sequence, uint16_t(0x1234));
    QCOMPARE(v->timestamp, uint32_t(960000));
    QCOMPARE(v->payloadType, uint8_t(111));
    QVERIFY(v->marker);
    QCOMPARE(v->ssrc, uint32_t(0xDEADBEEF));
  }
  void csrcExtensionAndPadding() {
    // CC=2, X=1, P=1: 2 CSRC (8 bytes), extension header (profile 2 bytes, length=1 word) + 4 bytes,
    // payload 4 bytes, 3 bytes padding (last byte = 3).
    std::vector<uint8_t> rest = {0, 0, 0, 1, 0, 0, 0, 2,  0xBE, 0xDE, 0, 1,  9, 9, 9, 9,
                                 10, 11, 12, 13,  0, 0, 3};
    const auto p = packet(0x80 | 0x20 | 0x10 | 2, 7, 5, rest);
    const auto v = parseRtpPacket(p);
    QVERIFY(v);
    QCOMPARE(v->payloadOffset, size_t(12 + 8 + 4 + 4));
    QCOMPARE(v->payloadSize, size_t(4));
  }
  void malformedPacketsAreRejected() {
    QVERIFY(!parseRtpPacket(std::vector<std::byte>(5)));
    QVERIFY(!parseRtpPacket(packet(0x40, 1, 1, {1})));              // version 1
    QVERIFY(!parseRtpPacket(packet(0x80, 1, 1, {})));               // empty payload
    QVERIFY(!parseRtpPacket(packet(0x80 | 0x10, 1, 1, {0xBE})));    // truncated extension header
    QVERIFY(!parseRtpPacket(packet(0x80 | 0x10, 1, 1, {0, 0, 0, 9, 1})));  // extension beyond packet
    QVERIFY(!parseRtpPacket(packet(0x80 | 0x20, 1, 1, {1, 9})));    // padding larger than packet
  }
  void handlerExtractsFramesAndKeepsControl() {
    OpusRtpDepacketizer d;
    const auto p = packet(0x80, 1, 48000, {0xAA, 0xBB});
    rtc::message_vector msgs;
    msgs.push_back(rtc::make_message(p.begin(), p.end()));
    auto ctl = rtc::make_message(std::vector<std::byte>(8), rtc::Message::Control);
    msgs.push_back(ctl);
    const auto bad = std::vector<std::byte>(3);
    msgs.push_back(rtc::make_message(bad.begin(), bad.end()));
    d.incoming(msgs, [](rtc::message_ptr) {});
    QCOMPARE(msgs.size(), size_t(2));
    QCOMPARE(msgs[0]->size(), size_t(2));
    QCOMPARE(uint8_t(msgs[0]->at(0)), uint8_t(0xAA));
    QVERIFY(msgs[0]->frameInfo);
    QCOMPARE(msgs[0]->frameInfo->timestamp, uint32_t(48000));
    QCOMPARE(msgs[1]->type, rtc::Message::Control);
  }
};

QTEST_GUILESS_MAIN(OpusDepacketizerTest)
#include "opusdepacketizer_test.moc"
