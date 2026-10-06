#pragma once
// Core options of a libretro core, captured without a game (retro_set_environment + retro_init, then deinit),
// cached from the last capture, and the option set applied at launch (settings hierarchy resolved by the caller).
// Options come only from what the core reports (SET_CORE_OPTIONS_V2/_V1/SET_VARIABLES); nothing is invented.

#include <QList>
#include <QMap>
#include <QString>

#include "emulator_backend.h"
#include "system_manifest.h"

namespace framebeam::emu {

struct CoreProbe {
  bool ok = false;
  QString error;
  CoreInfo info;
  QList<CoreOption> options;
  QList<CoreOptionCategory> categories;
};

// Loads the core WITHOUT a game, reads info and options, unloads it again. Only one core can be loaded per process
// (LibretroBackend): fails with an error while another one is loaded (e.g. a running game).
CoreProbe probeCore(const QString& corePath, const QString& systemDir, const QString& saveDir);

// JSON cache of a probe (written by the caller after a successful probe, read when probing is not possible).
QByteArray coreProbeToJson(const CoreProbe& probe);
CoreProbe coreProbeFromJson(const QByteArray& json);

// Options FrameBeam controls and that are not editable by the user: manifest-locked keys (render mode, screen
// layout, OSD ...) and the firmware options (sysfile mode, firmware path).
bool isLockedCoreOption(const SystemManifest& manifest, const QString& key);

// Core options at launch: manifest defaults < user overrides (already merged game > system > global, only explicit
// values; "framebeam.*" keys are ignored) < firmware options < locked manifest values.
QMap<QString, QString> launchCoreOptions(const SystemManifest& manifest, const QMap<QString, QString>& userOverrides,
                                         const QMap<QString, QString>& firmwareOptions);

}  // namespace framebeam::emu
