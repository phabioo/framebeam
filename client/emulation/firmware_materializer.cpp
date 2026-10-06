#include "firmware_materializer.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

namespace framebeam::emu {

bool materializeFirmware(const FirmwareSpec& spec, const QMap<QString, QString>& cachePathById, const QString& systemDir,
                         QStringList* writtenIds, QString* error) {
  const auto fail = [&](const QString& m) {
    if (error != nullptr) *error = m;
    return false;
  };
  if (writtenIds != nullptr) writtenIds->clear();
  if (!QDir().mkpath(systemDir)) return fail(QStringLiteral("System directory cannot be created"));
  for (auto it = cachePathById.cbegin(); it != cachePathById.cend(); ++it) {
    const FirmwareFile* file = spec.fileById(it.key());
    if (file == nullptr) return fail(QStringLiteral("Unknown firmware file: ") + it.key());
    QFile src(it.value());
    if (!src.open(QIODevice::ReadOnly)) return fail(QStringLiteral("Cached firmware file %1 is not readable").arg(it.key()));
    const QByteArray data = src.readAll();
    src.close();
    const QString dest = QDir(systemDir).filePath(file->name);
    QFile existing(dest);
    bool same = false;
    if (existing.exists() && existing.size() == data.size() && existing.open(QIODevice::ReadOnly)) {
      same = existing.readAll() == data;
      existing.close();
    }
    if (!same) {
      QSaveFile out(dest);
      if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
        return fail(QStringLiteral("Firmware file %1 cannot be written to the system directory").arg(file->name));
      }
    }
    if (writtenIds != nullptr) writtenIds->append(it.key());
  }
  return true;
}

QMap<QString, QString> firmwareCoreOptions(const FirmwareSpec& spec, bool native, const QStringList& materializedIds) {
  QMap<QString, QString> opts;
  if (spec.sysfileOption.isEmpty()) return opts;
  opts.insert(spec.sysfileOption, native ? spec.sysfileNative : spec.sysfileBuiltin);
  if (native) {
    for (const FirmwareFile& f : spec.files) {
      if (!f.coreOption.isEmpty() && materializedIds.contains(f.id)) opts.insert(f.coreOption, f.name);
    }
  }
  return opts;
}

}  // namespace framebeam::emu
