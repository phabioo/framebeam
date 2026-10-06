// Smoke test for the Phase 4 media dependencies: libdatachannel (SDP with H264 + opus),
// libavcodec H.264 encode/decode and libopus encode/decode. Plain QtTest, no network.
#include <QtTest>

#include <rtc/rtc.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <opus/opus.h>
}

#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int kWidth = 256;
constexpr int kHeight = 384;  // DS: both screens stacked

struct CodecCtxDeleter { void operator()(AVCodecContext *c) const { avcodec_free_context(&c); } };
struct FrameDeleter { void operator()(AVFrame *f) const { av_frame_free(&f); } };
struct PacketDeleter { void operator()(AVPacket *p) const { av_packet_free(&p); } };
using CodecCtx = std::unique_ptr<AVCodecContext, CodecCtxDeleter>;
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;

CodecCtx openEncoder(QString *name)
{
    for (const char *n : {"libopenh264", "libx264"}) {
        const AVCodec *codec = avcodec_find_encoder_by_name(n);
        if (!codec) continue;
        CodecCtx ctx(avcodec_alloc_context3(codec));
        ctx->width = kWidth;
        ctx->height = kHeight;
        ctx->time_base = AVRational{1, 60};
        ctx->framerate = AVRational{60, 1};
        ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        ctx->bit_rate = 2'000'000;
        ctx->gop_size = 120;
        ctx->max_b_frames = 0;
        AVDictionary *opts = nullptr;
        if (std::string(n) == "libx264") {
            av_dict_set(&opts, "preset", "ultrafast", 0);
            av_dict_set(&opts, "tune", "zerolatency", 0);
            av_dict_set(&opts, "profile", "baseline", 0);
        }
        const int rc = avcodec_open2(ctx.get(), codec, &opts);
        av_dict_free(&opts);
        if (rc == 0) { *name = QString::fromLatin1(n); return ctx; }
    }
    return nullptr;
}
}  // namespace

class MediaDepsTest : public QObject
{
    Q_OBJECT
private slots:
    void webrtcOfferContainsH264AndOpus()
    {
        rtc::Configuration config;  // no ICE servers: host candidates only, nothing leaves the machine
        rtc::PeerConnection pc(config);

        rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
        video.addH264Codec(96);
        auto videoTrack = pc.addTrack(video);
        rtc::Description::Audio audio("audio", rtc::Description::Direction::SendOnly);
        audio.addOpusCodec(111);
        auto audioTrack = pc.addTrack(audio);
        QVERIFY(videoTrack && audioTrack);

        pc.setLocalDescription(rtc::Description::Type::Offer);
        const auto sdp = pc.localDescription();
        QVERIFY(sdp.has_value());
        const QString text = QString::fromStdString(std::string(*sdp));
        QVERIFY2(text.contains("H264"), qPrintable(text));
        QVERIFY2(text.contains("opus", Qt::CaseInsensitive), qPrintable(text));
        pc.close();
    }

    void h264EncodeDecode()
    {
        QString encoderName;
        CodecCtx enc = openEncoder(&encoderName);
        QVERIFY2(enc, "neither libopenh264 nor libx264 could be opened");
        qInfo() << "H.264 encoder:" << encoderName;

        Frame frame(av_frame_alloc());
        frame->format = AV_PIX_FMT_YUV420P;
        frame->width = kWidth;
        frame->height = kHeight;
        QCOMPARE(av_frame_get_buffer(frame.get(), 0), 0);

        // Black frame, produced through swscale from RGB24 to also cover libswscale.
        std::vector<uint8_t> rgb(static_cast<size_t>(kWidth) * kHeight * 3, 0);
        SwsContext *sws = sws_getContext(kWidth, kHeight, AV_PIX_FMT_RGB24, kWidth, kHeight,
                                         AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
        QVERIFY(sws);

        const AVCodec *dec = avcodec_find_decoder_by_name("h264");
        QVERIFY(dec);
        CodecCtx decCtx(avcodec_alloc_context3(dec));
        QCOMPARE(avcodec_open2(decCtx.get(), dec, nullptr), 0);

        Packet pkt(av_packet_alloc());
        Frame out(av_frame_alloc());
        int packets = 0, decoded = 0;
        auto drainDecoder = [&] {
            while (avcodec_receive_frame(decCtx.get(), out.get()) == 0) {
                QCOMPARE(out->width, kWidth);
                QCOMPARE(out->height, kHeight);
                ++decoded;
            }
        };
        auto drainEncoder = [&] {
            while (avcodec_receive_packet(enc.get(), pkt.get()) == 0) {
                ++packets;
                QCOMPARE(avcodec_send_packet(decCtx.get(), pkt.get()), 0);
                av_packet_unref(pkt.get());
                drainDecoder();
            }
        };
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(av_frame_make_writable(frame.get()), 0);
            const uint8_t *src[1] = {rgb.data()};
            const int srcStride[1] = {kWidth * 3};
            sws_scale(sws, src, srcStride, 0, kHeight, frame->data, frame->linesize);
            frame->pts = i;
            QCOMPARE(avcodec_send_frame(enc.get(), frame.get()), 0);
            drainEncoder();
        }
        avcodec_send_frame(enc.get(), nullptr);  // flush
        drainEncoder();
        avcodec_send_packet(decCtx.get(), nullptr);
        drainDecoder();
        sws_freeContext(sws);
        QVERIFY2(packets >= 1, "encoder produced no packet");
        QVERIFY2(decoded >= 1, "decoder produced no frame");
    }

    void opusEncodeDecode()
    {
        constexpr int rate = 48000, channels = 2, frameSize = 960;  // 20 ms
        int err = 0;
        OpusEncoder *enc = opus_encoder_create(rate, channels, OPUS_APPLICATION_AUDIO, &err);
        QCOMPARE(err, OPUS_OK);
        OpusDecoder *dec = opus_decoder_create(rate, channels, &err);
        QCOMPARE(err, OPUS_OK);
        opus_encoder_ctl(enc, OPUS_SET_BITRATE(96000));

        std::vector<opus_int16> pcm(static_cast<size_t>(frameSize) * channels, 0);
        std::vector<unsigned char> packet(1500);
        const int n = opus_encode(enc, pcm.data(), frameSize, packet.data(), static_cast<opus_int32>(packet.size()));
        QVERIFY(n > 0);
        std::vector<opus_int16> back(pcm.size(), 1);
        const int samples = opus_decode(dec, packet.data(), n, back.data(), frameSize, 0);
        QCOMPARE(samples, frameSize);
        opus_encoder_destroy(enc);
        opus_decoder_destroy(dec);
    }
};

QTEST_GUILESS_MAIN(MediaDepsTest)
#include "media_deps_test.moc"
