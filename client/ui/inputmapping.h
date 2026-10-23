#pragma once
// Pure input helpers of the game view (no Qt Quick): keyboard -> joypad mask and
// item coordinates -> normalized frame position for the touch screen.

#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QtGlobal>
#include <optional>
#include <QString>

#include "system_manifest.h"

namespace uzume::ui {

// Qt::Key -> RETRO_DEVICE_ID_JOYPAD-Maske; 0 = not mapped.
// Arrows = D-pad, X=A, Z=B, S=X, A=Y, Q=L, W=R, Enter/Return=Start, Backspace=Select.
quint32 joypadMaskForKey(int qtKey);

// Holds the keyboard state (several keys at once). Auto-repeat is filtered by the caller.
// The key map comes from the keyboard controller profile (setMap); without one the standard map above applies.
class KeyboardJoypad {
 public:
  // true if the key is mapped (event was consumed).
  bool press(int qtKey);
  bool release(int qtKey);
  void clear() {
    held_.clear();
    mask_ = 0;
  }
  quint32 mask() const { return mask_; }
  // Qt::Key -> joypad mask; replaces the standard map. Keys that are held stay held and are re-evaluated.
  void setMap(const QHash<int, quint32>& map);
  void useStandardMap();

 private:
  quint32 maskFor(int qtKey) const;
  void recompute();

  QHash<int, quint32> map_;
  bool custom_ = false;
  QSet<int> held_;
  quint32 mask_ = 0;
};

// Rectangle into which a frame of size `frame` fits within `area` (centered, aspect ratio preserved).
// integerScale: integer factor if at least 1x fits and the space is used well
// (>= 75 % of the possible factor); otherwise a fractional factor (always drawn nearest-neighbour).
// strictInteger (option "Integer scaling" = On): whole-number factor whenever at least 1x fits (may leave borders),
// below 1x it falls back to fit.
QRectF fitFrame(const QSizeF& frame, const QSizeF& area, bool integerScale, bool strictInteger = false);

// Option "Aspect ratio": Core = the system's own shape, 4:3 = each screen drawn at 4:3, Stretch = fills the whole area.
enum class AspectMode { Core, Ratio43, Stretch };
AspectMode aspectModeFromString(const QString& value);  // "core" | "4:3" | "stretch"; anything else = Core
// Rectangle of a whole single-picture frame for the picture options (Stretch fills `area`, 4:3 fits a 4:3 frame).
QRectF fitPicture(const QSizeF& frame, const QSizeF& area, bool integerScale, bool strictInteger, AspectMode aspect);

// Item position -> normalized position in the whole frame (EmulatorBackend::setPointer).
// frameRect: where the frame lies in the item. clampToScreen=false: positions outside the touch screen
// yield nullopt (press start); true: clamped to the screen (hold and drag).
std::optional<QPointF> touchToFrame(const emu::DisplayProfile& profile, const QRectF& frameRect, const QPointF& itemPos,
                                    bool clampToScreen);

}  // namespace uzume::ui
