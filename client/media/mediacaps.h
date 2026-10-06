#pragma once

#include <QString>
#include <QStringList>

#include "hubprotocol.h"

namespace framebeam {

// What this Player can really do with libavcodec/libopus (verified by opening the codec), for the handshake.
struct MediaCapabilities {
  bool h264Encode = false;
  bool h264Decode = false;
  bool opus = false;
  QStringList encoders;  // H.264 encoders that opened, in preference order
};

// Opens (and closes) each candidate encoder once with a small test configuration; cached after the first call.
MediaCapabilities detectMediaCapabilities();
// Sets h264Encode/h264Decode/encoders/opus of the handshake data from detectMediaCapabilities().
void applyMediaCapabilities(HandshakeInfo* info);

}  // namespace framebeam
