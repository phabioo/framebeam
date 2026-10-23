#pragma once

#include <QByteArray>
#include <QString>

namespace uzume {

// Security code and other short codes shown to people (docs/reference/protocol.md, "Security code and setup code").
// Crockford base32 alphabet: 0123456789ABCDEFGHJKMNPQRSTVWXYZ (no I, L, O, U).
inline constexpr char kCrockfordAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

// True if every character of `s` (already normalized) is in the Crockford alphabet. Empty is false.
bool isCrockford(const QString& s);

// What a person types or pastes: spaces and hyphens removed, upper case. Used for security, setup and invite codes.
QString normalizeCode(const QString& text);

// "XXXX-XXXX-XXXX" from a normalized or typed code of 12 characters; any other length is returned normalized and
// ungrouped. Setup codes ("7H4K-2Q") are formatted with groupCode(code, 4).
QString formatSecurityCode(const QString& code);
QString groupCode(const QString& code, int groupSize);

// Security code of a server: the first 60 bits of the SHA-256 digest of the leaf certificate's DER encoding, as 12
// Crockford characters in three groups ("K7Q4-9WMP-2HXD"). Empty for a digest shorter than 8 bytes.
QString securityCodeFromDigest(const QByteArray& sha256);

// Same from a pinned fingerprint as stored in HubProfile::pinnedFingerprint (uppercase hex with colons; colons, spaces
// and case do not matter). Empty if `fingerprint` is not 32 bytes of hex.
QString securityCodeFromFingerprint(const QString& fingerprint);

}  // namespace uzume
