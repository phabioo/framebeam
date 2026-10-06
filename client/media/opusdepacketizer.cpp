#include "opusdepacketizer.h"

#include <algorithm>

namespace framebeam {

std::optional<RtpPayloadView> parseRtpPacket(std::span<const std::byte> p) {
  constexpr size_t kFixed = 12;
  if (p.size() < kFixed) {
    return std::nullopt;
  }
  const auto u8 = [&](size_t i) { return static_cast<uint8_t>(p[i]); };
  if ((u8(0) >> 6) != 2) {
    return std::nullopt;
  }
  const bool padding = (u8(0) & 0x20) != 0;
  const bool extension = (u8(0) & 0x10) != 0;
  const size_t csrc = u8(0) & 0x0F;

  RtpPayloadView v;
  v.marker = (u8(1) & 0x80) != 0;
  v.payloadType = u8(1) & 0x7F;
  v.sequence = static_cast<uint16_t>((u8(2) << 8) | u8(3));
  v.timestamp = (uint32_t(u8(4)) << 24) | (uint32_t(u8(5)) << 16) | (uint32_t(u8(6)) << 8) | u8(7);
  v.ssrc = (uint32_t(u8(8)) << 24) | (uint32_t(u8(9)) << 16) | (uint32_t(u8(10)) << 8) | u8(11);

  size_t off = kFixed + csrc * 4;
  if (extension) {
    if (p.size() < off + 4) {
      return std::nullopt;
    }
    const size_t words = (size_t(u8(off + 2)) << 8) | u8(off + 3);
    off += 4 + words * 4;
  }
  size_t end = p.size();
  if (padding) {
    const size_t pad = u8(p.size() - 1);
    if (pad == 0 || pad > end) {
      return std::nullopt;
    }
    end -= pad;
  }
  if (off >= end) {
    return std::nullopt;
  }
  v.payloadOffset = off;
  v.payloadSize = end - off;
  return v;
}

void OpusRtpDepacketizer::incoming(rtc::message_vector &messages, const rtc::message_callback &) {
  rtc::message_vector out;
  out.reserve(messages.size());
  for (auto &m : messages) {
    if (m->type == rtc::Message::Control) {
      out.push_back(std::move(m));
      continue;
    }
    const auto view = parseRtpPacket(std::span<const std::byte>(m->data(), m->size()));
    if (!view) {
      continue;  // drop malformed packets
    }
    auto frame = rtc::make_message(m->begin() + static_cast<std::ptrdiff_t>(view->payloadOffset),
                                   m->begin() + static_cast<std::ptrdiff_t>(view->payloadOffset + view->payloadSize));
    frame->type = rtc::Message::Binary;
    auto info = std::make_shared<rtc::FrameInfo>(view->timestamp);
    info->payloadType = view->payloadType;
    info->timestampSeconds = std::chrono::duration<double>(double(view->timestamp) / kClockRate);
    frame->frameInfo = std::move(info);
    out.push_back(std::move(frame));
  }
  messages.swap(out);
}

}  // namespace framebeam
