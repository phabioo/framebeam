#pragma once

#include <string>
#include <string_view>

namespace framebeam {

// Build identity of the FrameBeam Player (CMake cache vars FRAMEBEAM_VERSION / FRAMEBEAM_CHANNEL / FRAMEBEAM_COMMIT).
std::string_view playerVersion() noexcept;  // full SemVer, e.g. "0.3.0-beta.57" or "0.3.0-dev"
std::string_view playerChannel() noexcept;  // compiled default update channel: "stable" | "beta" | "dev"
std::string_view playerCommit() noexcept;   // may be empty

// One JSON object (no trailing newline), for `framebeam_player --version-json`:
// {"product":"player","version":"...","channel":"...","commit":"...","protocol_version":1,"min_protocol_version":1}
std::string playerVersionJson();

}  // namespace framebeam
