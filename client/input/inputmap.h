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
// System profile 3ds: the nds buttons plus ZL = L2 (bit 12), ZR = R2 (bit 13), the circle pad and the C-stick, which the backend serves
// as RETRO_DEVICE_ANALOG (left / right stick) from the bits above the 16 joypad ids. In the nds profile the circle pad
// inputs act as a second D-pad.
constexpr quint32 kLibretroCStickUp = 1u << 16;
constexpr quint32 kLibretroCStickDown = 1u << 17;
constexpr quint32 kLibretroCStickLeft = 1u << 18;
constexpr quint32 kLibretroCStickRight = 1u << 19;
// Circle pad = left analog (RETRO_DEVICE_INDEX_ANALOG_LEFT), bits 20..23.
constexpr quint32 kLibretroCirclePadUp = 1u << 20;
constexpr quint32 kLibretroCirclePadDown = 1u << 21;
constexpr quint32 kLibretroCirclePadLeft = 1u << 22;
constexpr quint32 kLibretroCirclePadRight = 1u << 23;
quint32 threeDsLibretroMask(quint32 inputMask);
// Mask for a manifest input_profile ("nds", "3ds"); unknown profiles use the nds buttons.
quint32 libretroMaskFor(const QString& inputProfile, quint32 inputMask);

// token -> FrameBeam input mask (several inputs may share a token).
QHash<QString, quint32> compileBindings(const Bindings& bindings);
// Qt::Key -> libretro joypad mask of the nds system profile (keyboard profile).
QHash<int, quint32> ndsKeyMap(const Bindings& bindings);
QHash<int, quint32> keyMapFor(const QString& inputProfile, const Bindings& bindings);

}  // namespace framebeam::input
