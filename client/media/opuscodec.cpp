#include "opuscodec.h"

#include <opus/opus.h>

namespace framebeam {

OpusFramer::OpusFramer() = default;
OpusFramer::~OpusFramer() { close(); }

bool OpusFramer::open(int bitrate) {
  close();
  int err = 0;
  enc_ = opus_encoder_create(kOpusRate, kOpusChannels, OPUS_APPLICATION_AUDIO, &err);
  if (err != OPUS_OK || !enc_) {
    enc_ = nullptr;
    return false;
  }
  opus_encoder_ctl(enc_, OPUS_SET_BITRATE(bitrate));
  opus_encoder_ctl(enc_, OPUS_SET_VBR(1));
  opus_encoder_ctl(enc_, OPUS_SET_SIGNAL(OPUS_AUTO));
  return true;
}

void OpusFramer::close() {
  if (enc_) {
    opus_encoder_destroy(enc_);
    enc_ = nullptr;
  }
  reset();
}

void OpusFramer::reset() {
  pending_.clear();
  resampler_.reset();
}

void OpusFramer::push(const QByteArray& pcm, int sampleRate, std::vector<std::vector<uint8_t>>& packets) {
  if (!enc_ || sampleRate <= 0) {
    return;
  }
  if (sampleRate != rate_) {
    rate_ = sampleRate;
    resampler_.setRates(sampleRate, kOpusRate);
    pending_.clear();
  }
  pending_.append(resampler_.process(pcm));
  while (pending_.size() >= kOpusFrameBytes) {
    uint8_t buf[1500];
    const int n = opus_encode(enc_, reinterpret_cast<const opus_int16*>(pending_.constData()), kOpusFrameSamples, buf, sizeof(buf));
    pending_.remove(0, kOpusFrameBytes);
    if (n > 0) {
      packets.emplace_back(buf, buf + n);
    }
  }
}

OpusDepacker::OpusDepacker() = default;
OpusDepacker::~OpusDepacker() {
  if (dec_) {
    opus_decoder_destroy(dec_);
  }
}

bool OpusDepacker::open() {
  int err = 0;
  dec_ = opus_decoder_create(kOpusRate, kOpusChannels, &err);
  if (err != OPUS_OK) {
    dec_ = nullptr;
  }
  return dec_ != nullptr;
}

QByteArray OpusDepacker::decode(const uint8_t* data, size_t size) {
  if (!dec_) {
    return {};
  }
  QByteArray out(5760 * kOpusChannels * int(sizeof(int16_t)), Qt::Uninitialized);  // max 120 ms
  const int n = opus_decode(dec_, data, static_cast<opus_int32>(size), reinterpret_cast<opus_int16*>(out.data()), 5760, 0);
  if (n <= 0) {
    return {};
  }
  out.resize(n * kOpusChannels * int(sizeof(int16_t)));
  return out;
}

}  // namespace framebeam
