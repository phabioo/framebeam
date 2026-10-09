#include "semver.h"

#include <QRegularExpression>

namespace framebeam::update {

namespace {

bool isNumeric(const QString& s) {
  if (s.isEmpty()) return false;
  for (const QChar c : s) {
    if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
  }
  return true;
}

int compareIdent(const QString& a, const QString& b) {
  const bool an = isNumeric(a);
  const bool bn = isNumeric(b);
  if (an && bn) {
    // Numeric identifiers may exceed 64 bits: compare by length, then lexically.
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return a < b ? -1 : (a > b ? 1 : 0);
  }
  if (an != bn) return an ? -1 : 1;  // numeric < alphanumeric
  return a < b ? -1 : (a > b ? 1 : 0);
}

}  // namespace

std::optional<SemVer> SemVer::parse(const QString& text) {
  static const QRegularExpression re(QStringLiteral(
      "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)"
      "(?:-((?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)(?:\\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?"
      "(?:\\+([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?$"));
  if (text.size() > 128) return std::nullopt;
  const QRegularExpressionMatch m = re.match(text);
  if (!m.hasMatch()) return std::nullopt;
  SemVer v;
  bool ok1 = false, ok2 = false, ok3 = false;
  v.major = m.captured(1).toLongLong(&ok1);
  v.minor = m.captured(2).toLongLong(&ok2);
  v.patch = m.captured(3).toLongLong(&ok3);
  if (!ok1 || !ok2 || !ok3) return std::nullopt;  // component beyond 63 bits
  if (!m.captured(4).isEmpty()) v.pre = m.captured(4).split(QLatin1Char('.'));
  return v;
}

int SemVer::compare(const SemVer& a, const SemVer& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  if (a.pre.isEmpty() && b.pre.isEmpty()) return 0;
  if (a.pre.isEmpty()) return 1;
  if (b.pre.isEmpty()) return -1;
  const qsizetype n = qMin(a.pre.size(), b.pre.size());
  for (qsizetype i = 0; i < n; ++i) {
    const int c = compareIdent(a.pre.at(i), b.pre.at(i));
    if (c != 0) return c;
  }
  if (a.pre.size() == b.pre.size()) return 0;
  return a.pre.size() < b.pre.size() ? -1 : 1;
}

QString SemVer::toString() const {
  QString s = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
  if (!pre.isEmpty()) s += QLatin1Char('-') + pre.join(QLatin1Char('.'));
  return s;
}

std::optional<int> compareVersions(const QString& a, const QString& b) {
  const auto va = SemVer::parse(a);
  const auto vb = SemVer::parse(b);
  if (!va || !vb) return std::nullopt;
  return SemVer::compare(*va, *vb);
}

CoreVersionVerdict coreVersionVerdict(const QString& coreVersion, const QString& expectedVersion) {
  const QString expected = expectedVersion.trimmed();
  if (expected.isEmpty()) return CoreVersionVerdict::Compatible;  // the Hub accepts any version
  const auto strip = [](QString v) {
    v = v.trimmed();
    return v.startsWith(QLatin1Char('v')) ? v.mid(1) : v;
  };
  if (strip(coreVersion) == strip(expected)) return CoreVersionVerdict::Compatible;
  // Hub build ids (`2026.10.09`, `2026.10.10.2`, ADR 0020 D3) are dates, not semver: never a major-version block.
  static const QRegularExpression buildId(QStringLiteral("^[0-9]{4}\\.[0-9]{2}\\.[0-9]{2}(\\.[0-9]+)?$"));
  if (buildId.match(strip(coreVersion)).hasMatch() || buildId.match(strip(expected)).hasMatch()) return CoreVersionVerdict::Warn;
  const auto a = SemVer::parse(strip(coreVersion));
  const auto b = SemVer::parse(strip(expected));
  if (!a || !b) return CoreVersionVerdict::Warn;
  return a->major != b->major ? CoreVersionVerdict::Block : CoreVersionVerdict::Warn;
}

}  // namespace framebeam::update
