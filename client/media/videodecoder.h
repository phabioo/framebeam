#pragma once

#include <QImage>
#include <QString>
#include <cstddef>
#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace framebeam {

// H.264 decoder (libavcodec "h264", low delay) producing QImage frames (Format_RGB32).
class VideoDecoder {
 public:
  VideoDecoder();
  ~VideoDecoder();
  VideoDecoder(const VideoDecoder&) = delete;
  VideoDecoder& operator=(const VideoDecoder&) = delete;

  static bool isAvailable();  // libavcodec can open an H.264 decoder

  bool open();
  void close();
  // libavcodec name of the decoder that is open (e.g. "h264"); empty while closed.
  QString name() const;
  // Decodes one access unit (Annex B). False on a decode error: the caller requests a keyframe.
  bool decode(const uint8_t* data, size_t size, std::vector<QImage>& out);

 private:
  AVCodecContext* ctx_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* pkt_ = nullptr;
  SwsContext* sws_ = nullptr;
  int swsW_ = 0, swsH_ = 0, swsFmt_ = -1;
};

}  // namespace framebeam
