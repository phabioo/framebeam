#pragma once

#include <QString>
#include <QtGlobal>
#include <optional>

namespace framebeam {

// Diagnostics of one side of a Session (ADR 0006 D6). Plain struct; "unavailable" values are std::nullopt / empty.
// Link state of one viewer connection (host side per viewer, viewer side its single link).
struct ViewerLinkStats {
  QString viewerId;
  QString state;  // new | connecting | connected | disconnected | failed | closed
  std::optional<double> rttMs;
  QString connectionType;  // "direct (host|srflx|prflx)" | "relay (udp|tcp)" | empty (unknown), see rtcutil.h
  // Host side: the latest rx report this viewer sent over fb-diag (ADR 0012 D5, 0.6 D7). hasReport = false until the
  // first one arrives; fps and decoder stay unset for Players that do not send them yet.
  bool hasReport = false;
  double reportLoss = 0.0;                // 0..1
  double reportKbps = 0.0;                // video kbit/s the viewer received
  std::optional<double> reportFps;        // frames/s the viewer decoded
  QString reportDecoder;                  // decoder the viewer uses, e.g. "h264"
};

struct SessionStats {
  bool active = false;           // media path exists (host: encoder running; viewer: connected)
  double fps = 0.0;              // host: encoded frames/s, viewer: decoded frames/s
  int width = 0;
  int height = 0;
  QString encoderName;           // host only, e.g. "libx264"
  QString decoderName;           // viewer only: libavcodec decoder in use, e.g. "h264" (never a name that is not in use)
  QString codec;                 // "H264 + Opus"
  double videoBitrateKbps = 0.0; // measured over the last second(s), payload
  double audioBitrateKbps = 0.0;
  std::optional<double> rttMs;                // nullopt: unavailable
  QString connectionType;        // selected candidate pair: "direct (host)" | "direct (srflx)" | "direct (prflx)" | "relay (udp)" |
                                 // "relay (tcp)" | empty (unknown). Host: the worst of its viewers (relay > srflx/prflx > host).
  double targetBitrateKbps = 0.0; // host: current AIMD target of the encoder (ADR 0012 D5), 0 while the encoder is off
  std::optional<double> packetLossPercent;    // viewer: from RTP sequence numbers; host: unavailable
  int viewers = 0;               // host: connected viewers
  qint64 videoFrames = 0;        // total frames encoded (host) / decoded (viewer)
  qint64 audioFrames = 0;        // total 20 ms Opus frames sent / decoded
  int decodeErrors = 0;          // viewer
  qint64 droppedFrames = 0;      // host: video frames dropped because the encoder fell behind
  int keyframeRequests = 0;      // host: PLI received, viewer: PLI sent
};

}  // namespace framebeam
