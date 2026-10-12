#pragma once
// Mapping chain, pure and without SDL: physical binding token -> FrameBeam input (bit mask over
// frameBeamInputs()) -> input profile of the system manifest -> libretro mask (RETRO_DEVICE_ID_JOYPAD_*, bit = 1 << id).

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace framebeam::input {

using Bindings = QMap<QString, QStringList>;  // FrameBeam input id -> tokens (see core/controllerprofiles.h)

quint32 inputBit(const QString& inputId);  // 1 << inputIndex, 0 = unknown input
// Libretro targets of the system manifest "input"."map" ({"<FrameBeam input id>": "<target>"}): the RETRO_DEVICE_ID_JOYPAD_*
// names b, y, select, start, up, down, left, right, a, x, l, r, l2, r2, l3, r3 (bit = 1 << id) plus the analog targets
// the backend serves as RETRO_DEVICE_ANALOG from the bits above the 16 joypad ids: c_stick_* (right analog, bits 16..19)
// and circle_pad_* (left analog, bits 20..23), each with the suffixes _up, _down, _left, _right.
constexpr quint32 kLibretroCStickUp = 1u << 16;
constexpr quint32 kLibretroCStickDown = 1u << 17;
constexpr quint32 kLibretroCStickLeft = 1u << 18;
constexpr quint32 kLibretroCStickRight = 1u << 19;
constexpr quint32 kLibretroCirclePadUp = 1u << 20;
constexpr quint32 kLibretroCirclePadDown = 1u << 21;
constexpr quint32 kLibretroCirclePadLeft = 1u << 22;
constexpr quint32 kLibretroCirclePadRight = 1u << 23;
quint32 libretroTargetBit(const QString& target);  // 0 = unknown target

// Input profile tables (one per manifest input_profile), registered from the loaded system manifests by the code that
// owns both libraries (this library does not depend on emulation). Several systems may share a profile (gb and gbc) as
// long as their maps are identical; a different map under the same profile name is an error. `labels` are optional
// per-input texts for the "System button" column of the Controllers page (input id -> text).
bool registerInputProfile(const QString& profile, const QMap<QString, QString>& map, const QMap<QString, QString>& labels = {},
                          QString* error = nullptr);
void clearInputProfiles();
// Whether the profile maps the FrameBeam input (unknown profiles are looked up as "nds"); label is empty when the manifest sets none.
bool inputProfileMaps(const QString& profile, const QString& inputId);
QString inputProfileLabel(const QString& profile, const QString& inputId);
// Libretro mask for a manifest input_profile; an unknown or empty profile uses the table registered as "nds" (no table = 0).
quint32 libretroMaskFor(const QString& inputProfile, quint32 inputMask);

// token -> FrameBeam input mask (several inputs may share a token).
QHash<QString, quint32> compileBindings(const Bindings& bindings);
// Qt::Key -> libretro mask of the input profile (keyboard profile).
QHash<int, quint32> keyMapFor(const QString& inputProfile, const Bindings& bindings);

}  // namespace framebeam::input
