#pragma once
// Screen layouts of the game view (0.6 UI pass, decision D12; 0.19 package 6): the core delivers one frame with all
// screens in the arrangement of the system manifest; the app may redraw the screens stacked, side by side, one large and
// one small, or show one of them only, optionally swapped. The choice applies to the running game only (the Emulation
// default lives in Settings). Pure geometry, no Qt Quick.

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <optional>

#include "inputmapping.h"
#include "system_manifest.h"

namespace uzume::ui {

inline constexpr char kLayoutStacked[] = "stacked";
inline constexpr char kLayoutSide[] = "side";
inline constexpr char kLayoutLarge[] = "large";    // big first screen at twice the size, the other one small beside it
inline constexpr char kLayoutTop[] = "top";        // the first (top) screen only
inline constexpr char kLayoutBottom[] = "bottom";  // the second (bottom) screen only

// Layouts a system offers, derived from its screen count: two screens (NDS, 3DS) -> stacked | side | large | top |
// bottom, anything else none (single-screen systems show no layout switch).
QStringList screenLayoutsFor(int screenCount);
bool isScreenLayout(const QString& layout);
// "Swap screens" (the bottom screen is drawn where the top one would be) only changes layouts that show both screens.
bool layoutSupportsSwap(const QString& layout);
// Human-readable name ("Side by side"), used by menus and the toast "Layout: Side by side".
QString screenLayoutLabel(const QString& layout);
// The layout after `current` in the list the system offers (wraps around); `current` unknown -> the first one.
QString nextScreenLayout(const QStringList& layouts, const QString& current);

struct ScreenPlacement {
  QList<QRectF> targets;  // per screen of the profile: where it is drawn in the item; an empty rect = hidden
  QRectF content;         // bounding rectangle of everything drawn
};

// Places the screens of `profile` in `area` for `layout`. Screens keep their size ratio; the whole arrangement is scaled
// like fitFrame() (integer factors when they use the space well). An unknown layout counts as "stacked".
// "large": the first screen at 2x on the left, the second at 1x on its right, aligned to the bottom edge.
// swap: the second screen takes the place of the first (stacked: bottom above top; side: bottom on the left; large: the
// bottom screen is the large one); ignored by "top" and "bottom" (see layoutSupportsSwap).
// strictInteger and aspect are the picture options (see fitFrame / AspectMode): 4:3 draws every screen at 4:3 (same
// height, width = height * 4/3), Stretch scales the whole arrangement non-uniformly to fill `area`.
ScreenPlacement placeScreens(const emu::DisplayProfile& profile, const QString& layout, const QSizeF& area, bool integerScale,
                             bool strictInteger = false, AspectMode aspect = AspectMode::Core,
                             bool swap = false);

// Source rectangle of screen `index` inside a frame of size `frame` (the frame may be larger than the profile when
// the core renders at a higher internal resolution; the screens scale with it).
QRectF screenSourceRect(const emu::DisplayProfile& profile, const QSizeF& frame, int index);

// Item position -> normalized position in the whole frame for the touch screen, using the placement above. Outside the
// touch screen: nullopt (press start) or clamped to it (clamp = true, hold and drag). Hidden touch screen: nullopt.
std::optional<QPointF> touchToFrameFor(const emu::DisplayProfile& profile, const ScreenPlacement& placement, const QPointF& itemPos,
                                       bool clampToScreen);

// A frame from a remote Session carries no manifest. Systems with two stacked screens (the frame is clearly taller than
// wide) are split into two equal halves, anything else counts as a single screen.
emu::DisplayProfile profileForRemoteFrame(const QSizeF& frame);

}  // namespace uzume::ui
