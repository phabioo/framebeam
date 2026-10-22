#pragma once
// Phase of the start of a game (shared by PlayerController, GameStarter, GameDetail and CoreCatalog).

namespace uzume::ui {

enum class PlayPhase { None, Core, Firmware, Rom, Launching };

}  // namespace uzume::ui
