#pragma once
// Player log file: <data>/logs/player.log (all Qt logging categories, timestamp + level), chained to the previous
// message handler. Rotates at startup (player.log, player.1.log .. player.3.log) and when a file exceeds the cap.

#include <QString>

namespace framebeam::filelog {

// Installs the message handler. Until setDirectory() is called, lines are buffered (bounded) in memory.
void install();
// Data directory known: creates <dir>/logs, rotates, opens player.log, flushes the buffer. Returns false if the
// file cannot be opened (logging then continues to the chained handler only).
bool setDirectory(const QString& dataDir, qint64 maxBytes = 5 * 1024 * 1024);
QString path();  // current log file; empty when not active

// Pure helpers (tests).
void rotate(const QString& logPath, int keep = 3);  // player.log -> player.1.log -> ... -> player.<keep>.log
QString formatLine(QtMsgType type, const QString& category, const QString& message);

}  // namespace framebeam::filelog
