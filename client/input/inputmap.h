#pragma once
// Mapping chain, pure and without SDL: physical binding token -> FrameBeam input (bit mask over
// frameBeamInputs()) -> system profile "nds" -> libretro joypad mask (RETRO_DEVICE_ID_JOYPAD_*, bit = 1 << id).

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace framebeam::input {

using Bindings = QMap<QString, QStringList>;  // FrameBeam input id -> tokens (see core/controllerprofiles.h)

quint32 inputBit(const QString& inputId);  // 1 << inputIndex, 0 = unknown input
// System profile nds: FrameBeam input mask -> libretro joypad mask.
quint32 ndsLibretroMask(quint32 inputMask);

// token -> FrameBeam input mask (several inputs may share a token).
QHash<QString, quint32> compileBindings(const Bindings& bindings);
// Qt::Key -> libretro joypad mask of the nds system profile (keyboard profile).
QHash<int, quint32> ndsKeyMap(const Bindings& bindings);

}  // namespace framebeam::input
