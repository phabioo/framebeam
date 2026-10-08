#include "padtype.h"

#include <SDL3/SDL.h>

namespace framebeam::input {

namespace {
constexpr quint16 kVendorMicrosoft = 0x045e;
constexpr quint16 kVendorSony = 0x054c;
}  // namespace

PadType classifyPad(int sdlGamepadType, quint16 vendor, quint16 /*product*/) {
  switch (static_cast<SDL_GamepadType>(sdlGamepadType)) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
      return PadType::Xbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
      return PadType::PlayStation;
    case SDL_GAMEPAD_TYPE_STANDARD:
    case SDL_GAMEPAD_TYPE_UNKNOWN:
      if (vendor == kVendorMicrosoft) return PadType::Xbox;
      if (vendor == kVendorSony) return PadType::PlayStation;
      return PadType::Generic;
    default:  // Nintendo Switch / Joy-Con / GameCube and other known non-Xbox, non-PlayStation layouts
      return PadType::Generic;
  }
}

QString padTypeId(PadType t) {
  switch (t) {
    case PadType::Xbox: return QStringLiteral("xbox");
    case PadType::PlayStation: return QStringLiteral("playstation");
    case PadType::Generic: break;
  }
  return QStringLiteral("generic");
}

QString padTypeName(PadType t) {
  switch (t) {
    case PadType::Xbox: return QStringLiteral("Xbox");
    case PadType::PlayStation: return QStringLiteral("PlayStation");
    case PadType::Generic: break;
  }
  return QStringLiteral("Generic");
}

PadType padTypeFromId(const QString& id, PadType fallback) {
  if (id == QLatin1String("xbox")) return PadType::Xbox;
  if (id == QLatin1String("playstation")) return PadType::PlayStation;
  if (id == QLatin1String("generic")) return PadType::Generic;
  return fallback;
}

}  // namespace framebeam::input
