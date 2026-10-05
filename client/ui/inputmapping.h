#pragma once
// Pure input helpers of the game view (no Qt Quick): keyboard -> joypad mask and
// item coordinates -> normalized frame position for the touch screen.

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QtGlobal>
#include <optional>

#include "system_manifest.h"

namespace framebeam::ui {

// Qt::Key -> RETRO_DEVICE_ID_JOYPAD-Maske; 0 = not mapped.
// Arrows = D-pad, X=A, Z=B, S=X, A=Y, Q=L, W=R, Enter/Return=Start, Backspace=Select.
quint32 joypadMaskForKey(int qtKey);

// Holds the keyboard state (several keys at once). Auto-repeat is filtered by the caller.
class KeyboardJoypad {
 public:
  // true if the key is mapped (event was consumed).
  bool press(int qtKey);
  bool release(int qtKey);
  void clear() { mask_ = 0; }
  quint32 mask() const { return mask_; }

 private:
  quint32 mask_ = 0;
};

// Rectangle into which a frame of size `frame` fits within `area` (centered, aspect ratio preserved).
// integerScale: integer factor if at least 1x fits and the space is used well
// (>= 75 % of the possible factor); otherwise a fractional factor (always drawn nearest-neighbour).
QRectF fitFrame(const QSizeF& frame, const QSizeF& area, bool integerScale);

// Item position -> normalized position in the whole frame (EmulatorBackend::setPointer).
// frameRect: where the frame lies in the item. clampToScreen=false: positions outside the touch screen
// yield nullopt (press start); true: clamped to the screen (hold and drag).
std::optional<QPointF> touchToFrame(const emu::DisplayProfile& profile, const QRectF& frameRect, const QPointF& itemPos,
                                    bool clampToScreen);

}  // namespace framebeam::ui
