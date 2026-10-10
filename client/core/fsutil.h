#pragma once

#include <QJsonObject>
#include <QString>

namespace framebeam::fsutil {

// Moves a file that cannot be parsed aside to "<path>.corrupt-<yyyyMMdd-HHmmss>" (never overwrites an existing
// quarantine file) and logs a warning. Returns the new path, empty if the move failed.
QString quarantineCorruptFile(const QString& path, const QString& reason);

// Reads a JSON object file. Missing or unreadable file: empty object, no side effects. A file that exists but is
// not valid JSON / not an object is quarantined first (quarantineCorruptFile) so that the next save cannot
// silently overwrite it; the result is then an empty object.
QJsonObject readJsonObject(const QString& path);

// Replaces `to` with `from` (Qt file API, long-path safe on Windows): removes the target, then renames; if the
// rename fails the file is copied. The source is removed only on success. Does not touch `to` when `from` is missing.
bool replaceFile(const QString& from, const QString& to);

// Environment variable that holds a path, read through the wide API on Windows (qgetenv + fromLocal8Bit is lossy
// for characters outside the ANSI code page). Empty if unset.
QString envPath(const char* name);

}  // namespace framebeam::fsutil
