#include "securitycode.h"

#include <cstring>

namespace uzume {

bool isCrockford(const QString& s) {
  if (s.isEmpty()) {
    return false;
  }
  for (const QChar c : s) {
    if (c.unicode() > 127 || c.unicode() == 0 || std::strchr(kCrockfordAlphabet, static_cast<char>(c.unicode())) == nullptr) {
      return false;
    }
  }
  return true;
}

QString normalizeCode(const QString& text) {
  QString out;
  out.reserve(text.size());
  for (const QChar c : text) {
    if (c == QLatin1Char('-') || c.isSpace()) {
      continue;
    }
    out.append(c.toUpper());
  }
  return out;
}

QString groupCode(const QString& code, int groupSize) {
  const QString n = normalizeCode(code);
  if (groupSize <= 0) {
    return n;
  }
  QString out;
  for (qsizetype i = 0; i < n.size(); i += groupSize) {
    if (i > 0) {
      out.append(QLatin1Char('-'));
    }
    out.append(n.mid(i, groupSize));
  }
  return out;
}

QString formatSecurityCode(const QString& code) {
  const QString n = normalizeCode(code);
  return n.size() == 12 ? groupCode(n, 4) : n;
}

QString securityCodeFromDigest(const QByteArray& sha256) {
  if (sha256.size() < 8) {
    return {};
  }
  QString code;
  code.reserve(12);
  for (int i = 0; i < 12; ++i) {
    const int byte = (5 * i) / 8;
    const int window = (static_cast<quint8>(sha256.at(byte)) << 8) | static_cast<quint8>(sha256.at(byte + 1));
    const int index = (window >> (11 - (5 * i) % 8)) & 31;
    code.append(QLatin1Char(kCrockfordAlphabet[index]));
  }
  return groupCode(code, 4);
}

QString securityCodeFromFingerprint(const QString& fingerprint) {
  QString hex;
  for (const QChar c : fingerprint) {
    if (c != QLatin1Char(':') && !c.isSpace()) {
      hex.append(c);
    }
  }
  if (hex.size() != 64) {
    return {};
  }
  for (const QChar c : hex) {
    const char16_t u = c.toLower().unicode();
    if (!((u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'f'))) {
      return {};
    }
  }
  return securityCodeFromDigest(QByteArray::fromHex(hex.toLatin1()));
}

}  // namespace uzume
