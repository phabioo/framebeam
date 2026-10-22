#pragma once
// Ed25519 signature of the update index (same format and key as the Hub core index, ADR 0010):
// sig file = one line "ed25519 <key_id> <base64 sig>", key_id = first 16 hex of SHA-256 over the 32-byte public key.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace uzume::update {

// Compiled-in trusted public keys (raw 32 bytes), same list as the Hub's corepkg.DefaultTrustedKeys.
QList<QByteArray> defaultTrustedKeys();
// Parses base64 (std) public keys; invalid entries are reported in *error (first problem) and skipped.
QList<QByteArray> parsePublicKeys(const QStringList& base64Keys, QString* error = nullptr);
// Env UZUME_APP_TRUST_KEYS (comma separated base64) appended to the compiled-in keys.
QList<QByteArray> trustedKeysFromEnvironment();

QString keyId(const QByteArray& publicKey);  // 16 hex chars
// Raw Ed25519 verification via OpenSSL (public key 32 bytes, signature 64 bytes).
bool ed25519Verify(const QByteArray& publicKey, const QByteArray& message, const QByteArray& signature);

struct SigCheck {
  bool ok = false;
  QString error;
};
// Signing context of the updates index (domain separation, ADR 0016 amendment 2026-10-19). Same string as
// corepkg.ContextUpdatesIndex in the server.
inline constexpr char kUpdatesIndexContext[] = "uzume-updates-index-v1";

// Verifies the exact index bytes against the signature line; the key is selected by its id. Accepts the legacy line
// ("ed25519 <key_id> <sig>", raw bytes; transition) and the domain-separated line ("ed25519ctx <key_id> <context> <sig>").
SigCheck verifyIndexSignature(const QByteArray& index, const QByteArray& sigFile, const QList<QByteArray>& trustedKeys);

}  // namespace uzume::update
