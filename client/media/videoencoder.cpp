#include "videoencoder.h"

#include <QLoggingCategory>
#include <QtCore/qglobal.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/log.h>
#include <libavutil/version.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace framebeam {

namespace {
Q_LOGGING_CATEGORY(lcEnc, "framebeam.videoencoder")

AVPixelFormat srcFormat(RawPixelFormat f) { return f == RawPixelFormat::Rgb565 ? AV_PIX_FMT_RGB565LE : AV_PIX_FMT_BGR0; }

void quietFfmpegLog() {
  static const bool quiet = [] {
    av_log_set_level(AV_LOG_WARNING);  // x264 statistics at close are not wanted in tests/CLI output
    return true;
  }();
  (void)quiet;
}

// Options of h264_nvenc for the CPU and the CUDA input path alike.
void nvencOptions(AVDictionary** opts) {
  av_dict_set(opts, "preset", "p1", 0);
  av_dict_set(opts, "tune", "ull", 0);
  av_dict_set(opts, "rc", "cbr", 0);
  av_dict_set(opts, "zerolatency", "1", 0);
  av_dict_set(opts, "profile", "baseline", 0);
  av_dict_set(opts, "forced-idr", "1", 0);
}
}  // namespace

VideoEncoder::VideoEncoder() = default;
VideoEncoder::~VideoEncoder() { close(); }

QStringList VideoEncoder::preferredEncoders() {
  // FRAMEBEAM_H264_ENCODER=<ffmpeg encoder name> forces one encoder (tests/diagnostics, e.g. libopenh264 or libx264).
  const QString forced = qEnvironmentVariable("FRAMEBEAM_H264_ENCODER").trimmed();
  if (!forced.isEmpty()) {
    return {forced};
  }
  return {QStringLiteral("h264_nvenc"), QStringLiteral("h264_qsv"), QStringLiteral("h264_amf"), QStringLiteral("libopenh264"),
          QStringLiteral("libx264")};
}

bool VideoEncoder::gpuInputConfigured(QString* why) {
  const auto no = [why](const QString& reason) {
    if (why) {
      *why = reason;
    }
    return false;
  };
  if (qEnvironmentVariable("FRAMEBEAM_DISABLE_GPU_ENCODE") == QLatin1String("1")) {
    return no(QStringLiteral("FRAMEBEAM_DISABLE_GPU_ENCODE=1"));
  }
  const QString forced = qEnvironmentVariable("FRAMEBEAM_H264_ENCODER").trimmed();
  if (!forced.isEmpty()) {
    return no(QStringLiteral("FRAMEBEAM_H264_ENCODER=%1 forces the readback path").arg(forced));
  }
  return true;
}

bool VideoEncoder::cudaInputSupported() {
  static const bool supported = [] {
    const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (!codec) {
      return false;
    }
    bool framesContext = false;
    for (int i = 0;; ++i) {
      const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
      if (!config) {
        break;
      }
      if (config->pix_fmt == AV_PIX_FMT_CUDA && (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX) != 0) {
        framesContext = true;
      }
    }
    bool cudaDevice = false;
    for (AVHWDeviceType t = av_hwdevice_iterate_types(AV_HWDEVICE_TYPE_NONE); t != AV_HWDEVICE_TYPE_NONE;
         t = av_hwdevice_iterate_types(t)) {
      cudaDevice = cudaDevice || t == AV_HWDEVICE_TYPE_CUDA;
    }
    return framesContext && cudaDevice;
  }();
  return supported;
}

QStringList VideoEncoder::effectiveOrder(const QStringList& order, bool cudaDead) {
  const QStringList base = order.isEmpty() ? preferredEncoders() : order;
  if (!cudaDead) {
    return base;
  }
  QStringList withoutNvenc = base;
  withoutNvenc.removeAll(QStringLiteral("h264_nvenc"));
  return withoutNvenc.isEmpty() ? base : withoutNvenc;
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
  bitrate_ = 0;
  forceKeyframe_ = true;
  swsSrcFormat_ = -1;
  gpuInput_ = false;
}

bool VideoEncoder::open(int width, int height, int fps, int bitrate, const QStringList& order) {
  close();
  quietFfmpegLog();
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
    // Profile left unset: ffmpeg rejects ctx->profile = 66 ("Unsupported avctx->profile"); OpenH264 then picks its
    // baseline-compatible default (its "profile(578)" warning is informational).
    ctx->thread_count = 1;  // single slice per frame: one access unit, no multi-slice surprises
    av_dict_set(&opts, "rc_mode", "bitrate", 0);
    av_dict_set(&opts, "allow_skip_frames", "0", 0);
  } else if (name == QLatin1String("h264_nvenc")) {
    nvencOptions(&opts);
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
  bitrate_ = bitrate;
  swsFormat_ = pix;
  forceKeyframe_ = true;
  qCInfo(lcEnc) << "H.264 encoder" << name << width << "x" << height << "@" << fps << bitrate / 1000 << "kbit/s";
  return true;
}

bool VideoEncoder::openGpu(AVBufferRef* framesCtx, int fps, int bitrate, QString* why) {
  close();
  quietFfmpegLog();
  QString reason;
  QString& out = why ? *why : reason;
  const auto* frames = framesCtx && framesCtx->data ? reinterpret_cast<const AVHWFramesContext*>(framesCtx->data) : nullptr;
  if (!frames || frames->format != AV_PIX_FMT_CUDA || frames->sw_format != AV_PIX_FMT_RGB0 || frames->width < 16 ||
      frames->height < 16 || frames->width > 8192 || frames->height > 8192 || (frames->width & 1) != 0 ||
      (frames->height & 1) != 0) {
    out = QStringLiteral("not a CUDA RGB0 frames context");
    return false;
  }
  const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
  if (!codec) {
    out = QStringLiteral("h264_nvenc not compiled in");
    return false;
  }
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  if (!ctx) {
    out = QStringLiteral("out of memory");
    return false;
  }
  ctx->width = frames->width;
  ctx->height = frames->height;
  ctx->time_base = AVRational{1, fps};
  ctx->framerate = AVRational{fps, 1};
  ctx->pix_fmt = AV_PIX_FMT_CUDA;
  ctx->hw_frames_ctx = av_buffer_ref(framesCtx);
  ctx->bit_rate = bitrate;
  ctx->rc_max_rate = bitrate;
  ctx->rc_min_rate = bitrate;
  ctx->rc_buffer_size = bitrate / 8;
  ctx->gop_size = fps * 2;
  ctx->max_b_frames = 0;
  ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
  ctx->flags &= ~AV_CODEC_FLAG_GLOBAL_HEADER;  // SPS/PPS in-band
  if (!ctx->hw_frames_ctx) {
    avcodec_free_context(&ctx);
    out = QStringLiteral("out of memory");
    return false;
  }

  AVDictionary* opts = nullptr;
  nvencOptions(&opts);
  av_dict_set(&opts, "delay", "0", 0);  // each frame leaves in the same send/receive cycle (the default holds back 2)
  const int rc = avcodec_open2(ctx, codec, &opts);
  av_dict_free(&opts);
  if (rc < 0) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(rc, buf, sizeof buf);  // never av_err2str: a C compound literal, does not compile as C++ on MSVC
    out = QString::fromUtf8(buf);
    qCInfo(lcEnc).noquote() << QStringLiteral("Encoder h264_nvenc does not open with CUDA frames: %1").arg(out);
    avcodec_free_context(&ctx);
    return false;
  }
  ctx_ = ctx;
  pkt_ = av_packet_alloc();
  if (!pkt_) {
    close();
    out = QStringLiteral("out of memory");
    return false;
  }
  name_ = QStringLiteral("h264_nvenc");
  width_ = frames->width;
  height_ = frames->height;
  bitrate_ = bitrate;
  gpuInput_ = true;
  forceKeyframe_ = true;
  qCInfo(lcEnc).noquote() << QStringLiteral("H.264 encoder h264_nvenc %1 x %2 @ %3 %4 kbit/s (CUDA frames)")
                                 .arg(width_)
                                 .arg(height_)
                                 .arg(fps)
                                 .arg(bitrate / 1000);
  return true;
}

const void* VideoEncoder::gpuFramesKey() const {
  return gpuInput_ && ctx_ && ctx_->hw_frames_ctx ? ctx_->hw_frames_ctx->data : nullptr;
}

bool VideoEncoder::setBitrate(int bitrate) {
  if (!ctx_ || !supportsRuntimeBitrate() || bitrate < 50'000) {
    return false;
  }
  ctx_->bit_rate = bitrate;
  ctx_->rc_max_rate = bitrate;
  ctx_->rc_min_rate = bitrate;
  ctx_->rc_buffer_size = bitrate / 8;
  bitrate_ = bitrate;
  qCInfo(lcEnc) << "Bitrate now" << bitrate / 1000 << "kbit/s";
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
  markKeyframe(frame_);
  if (avcodec_send_frame(ctx_, frame_) < 0) {
    return false;
  }
  return drainPackets(out);
}

bool VideoEncoder::encodeGpu(const AVFrame* frame, int64_t pts, std::vector<EncodedVideoPacket>& out) {
  if (!ctx_ || !gpuInput_ || !frame || !frame->hw_frames_ctx || frame->hw_frames_ctx->data != gpuFramesKey()) {
    return false;
  }
  AVFrame* f = av_frame_clone(frame);  // a new reference: the shared frame is never mutated
  if (!f) {
    return false;
  }
  f->pts = pts;
  markKeyframe(f);
  const int rc = avcodec_send_frame(ctx_, f);
  av_frame_free(&f);
  if (rc < 0) {
    return false;
  }
  return drainPackets(out);
}

void VideoEncoder::markKeyframe(AVFrame* frame) {
  if (forceKeyframe_.exchange(false)) {
    frame->pict_type = AV_PICTURE_TYPE_I;
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 29, 100)
    frame->flags |= AV_FRAME_FLAG_KEY;  // encoders that look at the flag instead of pict_type (libopenh264, hw)
#else
    frame->key_frame = 1;
#endif
  } else {
    frame->pict_type = AV_PICTURE_TYPE_NONE;
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 29, 100)
    frame->flags &= ~AV_FRAME_FLAG_KEY;
#else
    frame->key_frame = 0;
#endif
  }
}

bool VideoEncoder::drainPackets(std::vector<EncodedVideoPacket>& out) {
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
