#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "hubprotocol.h"

namespace framebeam {

// Handshake feature: the Hub serves the raw signed core index at GET /cores/index and /cores/index.sig (ADR 0012 D6).
inline constexpr const char* kCoresIndexFeature = "cores_index_v1";

struct CoreIndexCheck {
  bool ok = false;
  QString error;  // short reason, never contains file contents
};

// Player-side check of a core package against the signed core index (format: server/internal/corepkg/index.go):
//   1. the Ed25519 signature over the exact index bytes verifies against `trustedKeys` (update::verifyIndexSignature),
//   2. the index parses (schema 1),
//   3. it holds exactly one entry for (core id, version, platform) of `pkg` whose file names, roles, sizes and
//      SHA-256 match the package metadata the Hub served.
CoreIndexCheck verifyCorePackageAgainstIndex(const QByteArray& index, const QByteArray& sigFile, const QList<QByteArray>& trustedKeys,
                                             const CorePackageInfo& pkg);

}  // namespace framebeam
