#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace framebeam::update {

// SemVer 2.0.0. Build metadata (+...) is parsed but ignored for precedence and for toString().
struct SemVer {
  qint64 major = 0;
  qint64 minor = 0;
  qint64 patch = 0;
  QStringList pre;  // pre-release identifiers

  static std::optional<SemVer> parse(const QString& text);
  // <0, 0, >0 by SemVer precedence (a pre-release sorts before its release; numeric identifiers < alphanumeric).
  static int compare(const SemVer& a, const SemVer& b);
  QString toString() const;  // without build metadata
};

// Both must be valid SemVer; otherwise nullopt.
std::optional<int> compareVersions(const QString& a, const QString& b);

}  // namespace framebeam::update
