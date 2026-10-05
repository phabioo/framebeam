#include "videoencoder.h"

#include <QLoggingCategory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace framebeam {

namespace {
Q_LOGGING_CATEGORY(lcEnc, "framebeam.videoencoder")

AVPixelFormat srcFormat(RawPixelFormat f) { return f == RawPixelFormat::Rgb565 ? AV_PIX_FMT_RGB565LE : AV_PIX_FMT_BGR0; }
}  // namespace

VideoEncoder::VideoEncoder() = default;
VideoEncoder::~VideoEncoder() { close(); }

QStringList VideoEncoder::preferredEncoders() {
  return {QStringLiteral("h264_nvenc"), QStringLiteral("h264_qsv"), QStringLiteral("h264_amf"), QStringLiteral("libopenh264"),
          QStringLiteral("libx264")};
}

void VideoEncoder::close() {
  if (sws_) {
    sws_freeContext(sws_);
    sws_ = nullptr;
  }
  av_frame_free(&frame_);
  av_packet_free(&pkt_);
  avcodec_free_context(&ctx_);
  name_.clear();
  forceKeyframe_ = true;
  swsSrcFormat_ = -1;
}

bool VideoEncoder::open(int width, int height, int fps, int bitrate, const QStringList& order) {
  close();
  static const bool quiet = [] {
    av_log_set_level(AV_LOG_WARNING);  // x264 statistics at close are not wanted in tests/CLI output
    return true;
  }();
  (void)quiet;
  if (width < 16 || height < 16 || width > 8192 || height > 8192) {
    return false;
  }
  for (const QString& n : order.isEmpty() ? preferredEncoders() : order) {
    if (openWith(n, width, height, fps, bitrate)) {
      return true;
    }
  }
  return false;
}

bool VideoEncoder::openWith(const QString& name, int width, int height, int fps, int bitrate) {
  const AVCodec* codec = avcodec_find_encoder_by_name(name.toLatin1().constData());
  if (!codec) {
    return false;
  }
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  if (!ctx) {
    return false;
  }
  // Hardware encoders take NV12 (QSV) or YUV420P; software encoders YUV420P.
  const AVPixelFormat pix = name == QLatin1String("h264_qsv") ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
  // 4:2:0 needs even dimensions: an odd size is scaled by one pixel.
  ctx->width = width & ~1;
  ctx->height = height & ~1;
  ctx->time_base = AVRational{1, fps};
  ctx->framerate = AVRational{fps, 1};
  ctx->pix_fmt = pix;
  ctx->bit_rate = bitrate;
  ctx->rc_max_rate = bitrate;
  ctx->rc_min_rate = bitrate;
  ctx->rc_buffer_size = bitrate / 8;
  ctx->gop_size = fps * 2;
  ctx->max_b_frames = 0;
  ctx->thread_count = 2;
  ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
  ctx->flags &= ~AV_CODEC_FLAG_GLOBAL_HEADER;  // SPS/PPS in-band

  AVDictionary* opts = nullptr;
  if (name == QLatin1String("libx264")) {
    av_dict_set(&opts, "preset", "ultrafast", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);
    av_dict_set(&opts, "profile", "baseline", 0);
    av_dict_set(&opts, "x264-params", "repeat-headers=1:nal-hrd=cbr:force-cfr=1", 0);
  } else if (name == QLatin1String("libopenh264")) {
    av_dict_set(&opts, "profile", "constrained_baseline", 0);
    av_dict_set(&opts, "rc_mode", "bitrate", 0);
    av_dict_set(&opts, "allow_skip_frames", "0", 0);
  } else if (name == QLatin1String("h264_nvenc")) {
    av_dict_set(&opts, "preset", "p1", 0);
    av_dict_set(&opts, "tune", "ull", 0);
    av_dict_set(&opts, "rc", "cbr", 0);
    av_dict_set(&opts, "zerolatency", "1", 0);
    av_dict_set(&opts, "profile", "baseline", 0);
    av_dict_set(&opts, "forced-idr", "1", 0);
  } else if (name == QLatin1String("h264_qsv")) {
    av_dict_set(&opts, "preset", "veryfast", 0);
    av_dict_set(&opts, "profile", "baseline", 0);
    av_dict_set(&opts, "async_depth", "1", 0);
    av_dict_set(&opts, "forced_idr", "1", 0);
  } else if (name == QLatin1String("h264_amf")) {
    av_dict_set(&opts, "usage", "ultralowlatency", 0);
    av_dict_set(&opts, "profile", "constrained_baseline", 0);
    av_dict_set(&opts, "rc", "cbr", 0);
    av_dict_set(&opts, "forced_idr", "1", 0);
  }
  const int rc = avcodec_open2(ctx, codec, &opts);
  av_dict_free(&opts);
  if (rc < 0) {
    qCInfo(lcEnc) << "Encoder" << name << "does not open";
    avcodec_free_context(&ctx);
    return false;
  }
  ctx_ = ctx;
  frame_ = av_frame_alloc();
  pkt_ = av_packet_alloc();
  if (!frame_ || !pkt_) {
    close();
    return false;
  }
  frame_->format = pix;
  frame_->width = ctx_->width;
  frame_->height = ctx_->height;
  if (av_frame_get_buffer(frame_, 0) < 0) {
    close();
    return false;
  }
  name_ = name;
  width_ = width;
  height_ = height;
  swsFormat_ = pix;
  forceKeyframe_ = true;
  qCInfo(lcEnc) << "H.264 encoder" << name << width << "x" << height << "@" << fps << bitrate / 1000 << "kbit/s";
  return true;
}

bool VideoEncoder::encode(const uint8_t* data, int stride, RawPixelFormat format, int64_t pts,
                          std::vector<EncodedVideoPacket>& out) {
  if (!ctx_ || !data) {
    return false;
  }
  const AVPixelFormat sf = srcFormat(format);
  if (!sws_ || swsSrcFormat_ != sf) {
    if (sws_) {
      sws_freeContext(sws_);
    }
    sws_ = sws_getContext(width_, height_, sf, ctx_->width, ctx_->height, static_cast<AVPixelFormat>(swsFormat_), SWS_FAST_BILINEAR,
                          nullptr, nullptr, nullptr);
    swsSrcFormat_ = sf;
    if (!sws_) {
      return false;
    }
  }
  if (av_frame_make_writable(frame_) < 0) {
    return false;
  }
  const uint8_t* srcData[4] = {data, nullptr, nullptr, nullptr};
  const int srcStride[4] = {stride, 0, 0, 0};
  sws_scale(sws_, srcData, srcStride, 0, height_, frame_->data, frame_->linesize);
  frame_->pts = pts;
  if (forceKeyframe_) {
    frame_->pict_type = AV_PICTURE_TYPE_I;
    forceKeyframe_ = false;
  } else {
    frame_->pict_type = AV_PICTURE_TYPE_NONE;
  }
  if (avcodec_send_frame(ctx_, frame_) < 0) {
    return false;
  }
  while (true) {
    const int rc = avcodec_receive_packet(ctx_, pkt_);
    if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
      break;
    }
    if (rc < 0) {
      return false;
    }
    EncodedVideoPacket p;
    p.data.assign(pkt_->data, pkt_->data + pkt_->size);
    p.keyframe = (pkt_->flags & AV_PKT_FLAG_KEY) != 0;
    p.pts = pkt_->pts;
    out.push_back(std::move(p));
    av_packet_unref(pkt_);
  }
  return true;
}

}  // namespace framebeam
