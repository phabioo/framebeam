#include "version.h"

#include "hubprotocol.h"

#ifndef FRAMEBEAM_VERSION_STR
#define FRAMEBEAM_VERSION_STR "0.0.0-dev"
#endif
#ifndef FRAMEBEAM_CHANNEL_STR
#define FRAMEBEAM_CHANNEL_STR "dev"
#endif
#ifndef FRAMEBEAM_COMMIT_STR
#define FRAMEBEAM_COMMIT_STR ""
#endif

namespace framebeam {

namespace {
std::string jsonEscape(std::string_view in) {
  std::string out;
  for (const char c : in) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}
}  // namespace

std::string_view playerVersion() noexcept { return FRAMEBEAM_VERSION_STR; }
std::string_view playerChannel() noexcept { return FRAMEBEAM_CHANNEL_STR; }
std::string_view playerCommit() noexcept { return FRAMEBEAM_COMMIT_STR; }

std::string playerVersionJson() {
  return "{\"product\":\"player\",\"version\":\"" + jsonEscape(playerVersion()) + "\",\"channel\":\"" +
         jsonEscape(playerChannel()) + "\",\"commit\":\"" + jsonEscape(playerCommit()) +
         "\",\"protocol_version\":" + std::to_string(kProtocolVersion) +
         ",\"min_protocol_version\":" + std::to_string(kMinProtocolVersion) + "}";
}

}  // namespace framebeam
