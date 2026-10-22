#include "version.h"

#include "hubprotocol.h"

#ifndef UZUME_VERSION_STR
#define UZUME_VERSION_STR "0.0.0-dev"
#endif
#ifndef UZUME_CHANNEL_STR
#define UZUME_CHANNEL_STR "dev"
#endif
#ifndef UZUME_COMMIT_STR
#define UZUME_COMMIT_STR ""
#endif

namespace uzume {

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

std::string_view playerVersion() noexcept { return UZUME_VERSION_STR; }
std::string_view playerChannel() noexcept { return UZUME_CHANNEL_STR; }
std::string_view playerCommit() noexcept { return UZUME_COMMIT_STR; }

std::string playerVersionJson() {
  return "{\"product\":\"player\",\"version\":\"" + jsonEscape(playerVersion()) + "\",\"channel\":\"" +
         jsonEscape(playerChannel()) + "\",\"commit\":\"" + jsonEscape(playerCommit()) +
         "\",\"protocol_version\":" + std::to_string(kProtocolVersion) +
         ",\"min_protocol_version\":" + std::to_string(kMinProtocolVersion) + ",\"msi_downgrade\":true}";
}

}  // namespace uzume
