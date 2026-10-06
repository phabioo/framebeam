#include "mediacaps.h"

#include <mutex>
#include <opus/opus.h>

#include "videodecoder.h"
#include "videoencoder.h"

namespace framebeam {

MediaCapabilities detectMediaCapabilities() {
  static std::once_flag once;
  static MediaCapabilities caps;
  std::call_once(once, []() {
    for (const QString& n : VideoEncoder::preferredEncoders()) {
      VideoEncoder e;
      if (e.open(256, 384, 60, 2'000'000, {n})) {
        caps.encoders.append(n);
      }
    }
    caps.h264Encode = !caps.encoders.isEmpty();
    caps.h264Decode = VideoDecoder::isAvailable();
    int err = 0;
    if (OpusEncoder* e = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &err); e != nullptr) {
      opus_encoder_destroy(e);
      OpusDecoder* d = opus_decoder_create(48000, 2, &err);
      caps.opus = d != nullptr;
      if (d) {
        opus_decoder_destroy(d);
      }
    }
  });
  return caps;
}

void applyMediaCapabilities(HandshakeInfo* info) {
  const MediaCapabilities c = detectMediaCapabilities();
  info->h264Encode = c.h264Encode;
  info->h264Decode = c.h264Decode;
  info->encoders = c.encoders;
  info->opus = c.opus;
}

}  // namespace framebeam
