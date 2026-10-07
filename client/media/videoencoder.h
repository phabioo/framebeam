#pragma once

#include <QString>
#include <QStringList>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace framebeam {

struct EncodedVideoPacket {
  std::vector<uint8_t> data;  // Annex B (start codes), SPS/PPS in-band before keyframes
  bool keyframe = false;
  int64_t pts = 0;            // in 1/fps ticks as passed to encode()
};

enum class RawPixelFormat { Xrgb8888, Rgb565 };  // libretro formats, native endian

// H.264 encoder over libavcodec: low latency, no B-frames, constant bitrate, GOP 2 s (ADR 0006 D5).
class VideoEncoder {
 public:
  VideoEncoder();
  ~VideoEncoder();
  VideoEncoder(const VideoEncoder&) = delete;
  VideoEncoder& operator=(const VideoEncoder&) = delete;

  // Preference per ADR 0006 D5.
  static QStringList preferredEncoders();

  // Opens the first encoder of `order` (empty: preferredEncoders()) that works. False if none opens.
  bool open(int width, int height, int fps = 60, int bitrate = 2'000'000, const QStringList& order = {});
  void close();
  bool isOpen() const { return ctx_ != nullptr; }
  QString name() const { return name_; }
  int width() const { return width_; }
  int height() const { return height_; }

  // Target bitrate in bit/s of the open encoder.
  int bitrate() const { return bitrate_; }
  // True if the encoder can change its bitrate while running (libx264: reconfigured by libavcodec on the next frame).
  bool supportsRuntimeBitrate() const { return name_ == QLatin1String("libx264"); }
  // Changes the bitrate without reopening; false (and nothing changed) if not supported or not open.
  bool setBitrate(int bitrate);

  // The next frame becomes an IDR frame (viewer joined, PLI). Thread-safe.
  void requestKeyframe() { forceKeyframe_ = true; }

  // Encodes one frame (size must match open()) and appends the finished packets to `out` (no B-frames: normally one).
  bool encode(const uint8_t* data, int stride, RawPixelFormat format, int64_t pts, std::vector<EncodedVideoPacket>& out);

 private:
  bool openWith(const QString& name, int width, int height, int fps, int bitrate);

  AVCodecContext* ctx_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* pkt_ = nullptr;
  SwsContext* sws_ = nullptr;
  int swsFormat_ = -1;
  QString name_;
  int width_ = 0;
  int height_ = 0;
  int bitrate_ = 0;
  int swsSrcFormat_ = -1;
  std::atomic<bool> forceKeyframe_{true};  // may be set from other threads
};

}  // namespace framebeam
