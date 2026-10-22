#pragma once
// Design system fonts (docs/design/uzume/design-system.md "Typography"): IBM Plex Sans 300/400/500/600 and IBM Plex Mono
// 400/500/600 as Qt resources of uzume_ui (OFL, client/ui/fonts/IBM-Plex-OFL.txt).

namespace uzume::ui {

// Registers the bundled fonts with QFontDatabase (idempotent) and sets IBM Plex Sans as the application font family,
// keeping the size and other properties of the current application font. Needs a QGuiApplication. Returns true when
// both families are available afterwards.
bool loadDesignFonts();

}  // namespace uzume::ui
