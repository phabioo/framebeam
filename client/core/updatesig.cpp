#include "updatesig.h"

#include <QCryptographicHash>
#include <QRegularExpression>

#include <openssl/evp.h>

namespace framebeam::update {

QList<QByteArray> defaultTrustedKeys() {
  // FrameBeam release key (2026-10-07); the private counterpart lives only in the secret FRAMEBEAM_SIGNING_KEY.
  return parsePublicKeys({QStringLiteral("XNEQ9t7C2AtGzhfsg+oZwDKp8G2SG0iLxAQGg1u86PI=")});
}

QList<QByteArray> parsePublicKeys(const QStringList& base64Keys, QString* error) {
  QList<QByteArray> out;
  for (const QString& k : base64Keys) {
    const QString t = k.trimmed();
    if (t.isEmpty()) continue;
    const QByteArray raw = QByteArray::fromBase64(t.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (raw.size() != 32) {
      if (error != nullptr && error->isEmpty()) *error = QStringLiteral("trusted key must be base64 of 32 bytes");
      continue;
    }
    out.append(raw);
  }
  return out;
}

QList<QByteArray> trustedKeysFromEnvironment() {
  QList<QByteArray> keys = defaultTrustedKeys();
  const QString extra = qEnvironmentVariable("FRAMEBEAM_PLAYER_TRUST_KEYS");
  if (!extra.isEmpty()) keys += parsePublicKeys(extra.split(QLatin1Char(','), Qt::SkipEmptyParts));
  return keys;
}

QString keyId(const QByteArray& publicKey) {
  return QString::fromLatin1(QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256).toHex().left(16));
}

bool ed25519Verify(const QByteArray& publicKey, const QByteArray& message, const QByteArray& signature) {
  if (publicKey.size() != 32 || signature.size() != 64) return false;
  EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                               reinterpret_cast<const unsigned char*>(publicKey.constData()), 32);
  if (pkey == nullptr) return false;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  bool ok = false;
  if (ctx != nullptr) {
    if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1) {
      ok = EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char*>(signature.constData()), 64,
                            reinterpret_cast<const unsigned char*>(message.constData()),
                            static_cast<size_t>(message.size())) == 1;
    }
    EVP_MD_CTX_free(ctx);
  }
  EVP_PKEY_free(pkey);
  return ok;
}

SigCheck verifyIndexSignature(const QByteArray& index, const QByteArray& sigFile, const QList<QByteArray>& trustedKeys) {
  SigCheck r;
  if (trustedKeys.isEmpty()) {
    r.error = QStringLiteral("no trusted signing key configured");
    return r;
  }
  QString line = QString::fromLatin1(sigFile);
  while (line.endsWith(QLatin1Char('\n')) || line.endsWith(QLatin1Char('\r'))) line.chop(1);
  if (line.contains(QLatin1Char('\n')) || line.contains(QLatin1Char('\r'))) {
    r.error = QStringLiteral("signature file must be a single line");
    return r;
  }
  const QStringList f = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  if (f.size() != 3 || f.at(0) != QLatin1String("ed25519")) {
    r.error = QStringLiteral("signature line must be: ed25519 <key_id> <base64 signature>");
    return r;
  }
  const QByteArray sig = QByteArray::fromBase64(f.at(2).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
  if (sig.size() != 64) {
    r.error = QStringLiteral("signature is not valid base64 of 64 bytes");
    return r;
  }
  for (const QByteArray& k : trustedKeys) {
    if (keyId(k) == f.at(1)) {
      if (!ed25519Verify(k, index, sig)) {
        r.error = QStringLiteral("signature does not match the index");
        return r;
      }
      r.ok = true;
      return r;
    }
  }
  r.error = QStringLiteral("signing key %1 is not trusted").arg(f.at(1));
  return r;
}

}  // namespace framebeam::update
