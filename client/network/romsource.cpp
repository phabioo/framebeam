#include "romsource.h"

namespace framebeam {

RomSourceChoice chooseRomSource(const GameEntry& game, const std::function<QString(const QString&)>& localPathForSha,
                                bool cacheHas) {
  if (localPathForSha) {
    // The primary file first, then the other known dumps of the same game.
    QStringList order{game.romSha256};
    for (const RomFileEntry& r : game.roms) {
      if (!order.contains(r.sha256)) order.append(r.sha256);
    }
    for (const QString& sha : std::as_const(order)) {
      const QString path = localPathForSha(sha);
      if (!path.isEmpty()) return {RomSource::Local, path};
    }
  }
  if (cacheHas) return {RomSource::Cache, {}};
  if (game.onHub) return {RomSource::Download, {}};
  return {RomSource::Missing, {}};
}

}  // namespace framebeam
