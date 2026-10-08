#pragma once
// Controller type for the "Button labels" glyph set (Controllers page): Xbox, PlayStation or Generic.
// The classification is pure (SDL gamepad type id + USB vendor/product), so it is testable without a device.

#include <QString>
#include <QtGlobal>

namespace framebeam::input {

enum class PadType { Generic, Xbox, PlayStation };

// sdlGamepadType = SDL_GamepadType as int (SDL_GetGamepadType); vendor/product = USB ids (0 = unknown).
// Xbox 360/One -> Xbox; PS3/PS4/PS5 -> PlayStation; Nintendo and other known types -> Generic. For SDL_GAMEPAD_TYPE_STANDARD
// and UNKNOWN the vendor id decides (Microsoft 0x045e -> Xbox, Sony 0x054c -> PlayStation, anything else Generic).
PadType classifyPad(int sdlGamepadType, quint16 vendor, quint16 product);

QString padTypeId(PadType t);    // "xbox" | "playstation" | "generic"
QString padTypeName(PadType t);  // "Xbox" | "PlayStation" | "Generic" (UI)
PadType padTypeFromId(const QString& id, PadType fallback = PadType::Generic);

}  // namespace framebeam::input
