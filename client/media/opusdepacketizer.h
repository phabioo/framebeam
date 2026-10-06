#pragma once

#include <cstdint>
#include <optional>
#include <rtc/rtc.hpp>
#include <span>

namespace framebeam {

// Result of parsing one RTP packet (RFC 3550): payload range inside the packet plus header fields.
struct RtpPayloadView {
  size_t payloadOffset = 0;
  size_t payloadSize = 0;
  uint8_t payloadType = 0;
  uint16_t sequence = 0;
  uint32_t timestamp = 0;
  uint32_t ssrc = 0;
  bool marker = false;
};

// Parses the fixed header, CSRC list, header extension and trailing padding. Returns nullopt for
// malformed packets (too short, version != 2, extension/padding beyond the packet, empty payload).
std::optional<RtpPayloadView> parseRtpPacket(std::span<const std::byte> packet);

// Opus RTP depacketizer (RFC 7587: one Opus frame per RTP packet). Replaces
// rtc::OpusRtpDepacketizer: that is a class template instantiation (AudioRtpDepacketizer<48000>), which
// the MSVC build of the libdatachannel DLL does not export. This class only uses exported, non-template
// base classes. RTCP (Control messages) passes through unchanged.
class OpusRtpDepacketizer final : public rtc::MediaHandler {
public:
  static constexpr uint32_t kClockRate = 48000;

  void incoming(rtc::message_vector &messages, const rtc::message_callback &send) override;
};

}  // namespace framebeam
