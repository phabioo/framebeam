#pragma once
// Screen layouts of the game view (0.6 UI pass, decision D12): the core delivers one frame with all screens in the
// arrangement of the system manifest; the Player may redraw the screens side by side or show the first one only. The
// choice applies to the running game only (the Emulation default lives in Settings). Pure geometry, no Qt Quick.

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <optional>

#include "system_manifest.h"

namespace framebeam::ui {

inline constexpr char kLayoutStacked[] = "stacked";
inline constexpr char kLayoutSide[] = "side";
inline constexpr char kLayoutTop[] = "top";

// Layouts a system offers, derived from its screen count: two screens -> stacked | side | top, anything else none
// (single-screen systems show no switch; systems with more screens, e.g. 3DS, get their own set later).
QStringList screenLayoutsFor(int screenCount);
bool isScreenLayout(const QString& layout);

struct ScreenPlacement {
  QList<QRectF> targets;  // per screen of the profile: where it is drawn in the item; an empty rect = hidden
  QRectF content;         // bounding rectangle of everything drawn
};

// Places the screens of `profile` in `area` for `layout`. Screens keep their size ratio; the whole arrangement is scaled
// like fitFrame() (integer factors when they use the space well). An unknown layout counts as "stacked".
ScreenPlacement placeScreens(const emu::DisplayProfile& profile, const QString& layout, const QSizeF& area, bool integerScale);

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

}  // namespace framebeam::ui
