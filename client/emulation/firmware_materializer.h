#pragma once
// Makes cached firmware files available where the core reads them (its libretro system directory) and derives
// the core options for the firmware mode. Data-driven by FirmwareSpec of the manifest; no console-specific logic.
// Contents are never logged.

#include <QMap>
#include <QString>
#include <QStringList>

#include "system_manifest.h"

namespace framebeam::emu {

// Copies the cached files (file id -> path in the firmware cache) into systemDir under the names of the manifest
// (e.g. bios7.bin). Existing files with identical content are left alone, others are replaced atomically.
// writtenIds receives the ids now present in systemDir. Unknown ids are an error.
bool materializeFirmware(const FirmwareSpec& spec, const QMap<QString, QString>& cachePathById, const QString& systemDir,
                         QStringList* writtenIds = nullptr, QString* error = nullptr);

// Core options for the firmware mode of the Hub: native -> sysfile option "native" and the per-file options
// (e.g. firmware path = materialized name) for materializedIds; builtin -> only the sysfile option "builtin".
// Empty if the core has no sysfile switch.
QMap<QString, QString> firmwareCoreOptions(const FirmwareSpec& spec, bool native, const QStringList& materializedIds);

}  // namespace framebeam::emu
