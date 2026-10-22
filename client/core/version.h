#pragma once

#include <string>
#include <string_view>

namespace uzume {

// Build identity of the Uzume (CMake cache vars UZUME_VERSION / UZUME_CHANNEL / UZUME_COMMIT).
std::string_view playerVersion() noexcept;  // full SemVer, e.g. "0.3.0-beta.57" or "0.3.0-dev"
std::string_view playerChannel() noexcept;  // compiled default update channel: "stable" | "beta" | "dev"
std::string_view playerCommit() noexcept;   // may be empty

// One JSON object (no trailing newline), for `uzume --version-json`:
// {"product":"player","version":"...","channel":"...","commit":"...","protocol_version":1,"min_protocol_version":1}
std::string playerVersionJson();

}  // namespace uzume
