#pragma once

#include <QList>
#include <QString>

#include "lua_sandbox.h"

namespace framebeam::plugins {

enum class PluginStatus { Loading, Running, Paused, Stopped };

// What the plugins are told about the running game (fb.game). Empty fields become nil in Lua.
struct GameContext {
  QString system;    // "nds", "3ds", ...
  QString code;      // game code, e.g. the 4-letter DS code or the 3DS title id
  QString sha256;
  QString title;
  QString romPath;   // game file for fb.rom (rom.read)
  QString language;  // game language for fb.i18n, e.g. "de", "en", "de-DE"
  bool shell = false;  // shell context: no game runs; fb.game is an empty table, memory/rom reads return nil
};

struct HostLimits {
  size_t memoryBytes = 64u * 1024 * 1024;
  qint64 instructions = 20'000'000;
  int wallMs = 500;
};

// Developer-console data (design 4d-4-2).
struct LogLine {
  qint64 timeMs = 0;  // ms since epoch
  LogLevel level = LogLevel::Info;
  QString text;
};
struct EventLine {
  qint64 timeMs = 0;
  QString plugin;  // emitting plugin
  QString name;    // event name ("kind" chip)
  QString text;    // short rendering of the value
};
struct WatchedRow {
  QString label;
  QString address;  // "0x08001A10"
  QString value;    // rendered per kind; "-" when not in the latest snapshot
};
struct ConsoleSnapshot {
  PluginStatus status = PluginStatus::Stopped;
  QString error;  // message + traceback of the error that paused the plugin; empty otherwise
  QList<LogLine> log;       // ring of 500
  QList<EventLine> events;  // ring of 200
  QList<WatchedRow> watched;
};

inline constexpr int kLogRing = 500;
inline constexpr int kEventRing = 200;

}  // namespace framebeam::plugins
