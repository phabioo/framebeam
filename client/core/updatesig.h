#pragma once
// Ed25519 signature of the update index (same format and key as the Hub core index, ADR 0010):
// sig file = one line "ed25519 <key_id> <base64 sig>", key_id = first 16 hex of SHA-256 over the 32-byte public key.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace framebeam::update {

// Compiled-in trusted public keys (raw 32 bytes), same list as the Hub's corepkg.DefaultTrustedKeys.
QList<QByteArray> defaultTrustedKeys();
// Parses base64 (std) public keys; invalid entries are reported in *error (first problem) and skipped.
QList<QByteArray> parsePublicKeys(const QStringList& base64Keys, QString* error = nullptr);
// Env FRAMEBEAM_PLAYER_TRUST_KEYS (comma separated base64) appended to the compiled-in keys.
QList<QByteArray> trustedKeysFromEnvironment();

QString keyId(const QByteArray& publicKey);  // 16 hex chars
// Raw Ed25519 verification via OpenSSL (public key 32 bytes, signature 64 bytes).
bool ed25519Verify(const QByteArray& publicKey, const QByteArray& message, const QByteArray& signature);

struct SigCheck {
  bool ok = false;
  QString error;
};
// Verifies the exact index bytes against the signature line; the key is selected by its id.
SigCheck verifyIndexSignature(const QByteArray& index, const QByteArray& sigFile, const QList<QByteArray>& trustedKeys);

}  // namespace framebeam::update
