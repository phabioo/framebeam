#pragma once
// Where the ROM of a game comes from at launch (0.12): a file in a library folder of this device (any of the game's
// ROM hashes), else the ROM cache, else a download when a file is stored on the Hub, else "missing on this device".
#include <QString>
#include <QStringList>
#include <functional>

#include "hubprotocol.h"

namespace uzume {

enum class RomSource { Local, Cache, Download, Missing };

struct RomSourceChoice {
  RomSource source = RomSource::Missing;
  QString localPath;  // Local: the file to start from (no copy into the cache)
};

// `localPathForSha` returns the path of a still-valid local file with that hash (empty = none); `cacheHas` tells
// whether the ROM cache holds the game's primary file. Both are cheap checks (no hashing).
RomSourceChoice chooseRomSource(const GameEntry& game, const std::function<QString(const QString&)>& localPathForSha,
                                bool cacheHas);

}  // namespace uzume
