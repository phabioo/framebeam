#include "videodecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <climits>
#include <cstring>

namespace framebeam {

VideoDecoder::VideoDecoder() = default;
VideoDecoder::~VideoDecoder() { close(); }

bool VideoDecoder::isAvailable() {
  VideoDecoder d;
  return d.open();
}

void VideoDecoder::close() {
  if (sws_) {
    sws_freeContext(sws_);
    sws_ = nullptr;
  }
  av_frame_free(&frame_);
  av_packet_free(&pkt_);
  avcodec_free_context(&ctx_);
  swsW_ = swsH_ = 0;
  swsFmt_ = -1;
}

bool VideoDecoder::open() {
  close();
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec) {
    return false;
  }
  ctx_ = avcodec_alloc_context3(codec);
  if (!ctx_) {
    return false;
  }
  ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
  ctx_->flags2 |= AV_CODEC_FLAG2_FAST;
  ctx_->thread_count = 1;  // frame threading would add latency
  if (avcodec_open2(ctx_, codec, nullptr) < 0) {
    close();
    return false;
  }
  frame_ = av_frame_alloc();
  pkt_ = av_packet_alloc();
  if (!frame_ || !pkt_) {
    close();
    return false;
  }
  return true;
}

bool VideoDecoder::decode(const uint8_t* data, size_t size, std::vector<QImage>& out) {
  if (!ctx_ || size == 0) {
    return false;
  }
  // The bitstream reader may read past the end: the payload goes into an FFmpeg-owned buffer with the zeroed
  // AV_INPUT_BUFFER_PADDING_SIZE bytes (av_new_packet), not straight from the caller's QByteArray.
  if (size > static_cast<size_t>(INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE) || av_new_packet(pkt_, static_cast<int>(size)) < 0) {
    return false;
  }
  std::memcpy(pkt_->data, data, size);
  const int sent = avcodec_send_packet(ctx_, pkt_);
  av_packet_unref(pkt_);
  if (sent < 0) {
    return false;
  }
  while (true) {
    const int rc = avcodec_receive_frame(ctx_, frame_);
    if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
      break;
    }
    if (rc < 0) {
      return false;
    }
    if (!sws_ || swsW_ != frame_->width || swsH_ != frame_->height || swsFmt_ != frame_->format) {
      if (sws_) {
        sws_freeContext(sws_);
      }
      sws_ = sws_getContext(frame_->width, frame_->height, static_cast<AVPixelFormat>(frame_->format), frame_->width, frame_->height,
                            AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
      swsW_ = frame_->width;
      swsH_ = frame_->height;
      swsFmt_ = frame_->format;
      if (!sws_) {
        return false;
      }
    }
    QImage img(frame_->width, frame_->height, QImage::Format_RGB32);
    uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    const int dstStride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
    sws_scale(sws_, frame_->data, frame_->linesize, 0, frame_->height, dst, dstStride);
    out.push_back(std::move(img));
    av_frame_unref(frame_);
  }
  return true;
}

}  // namespace framebeam
