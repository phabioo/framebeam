#pragma once
// Reine Eingabe-Helfer der Spielansicht (ohne Qt Quick): Tastatur -> Joypad-Maske und
// Item-Koordinaten -> normierte Frame-Position fuer den Touch-Screen.

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QtGlobal>
#include <optional>

#include "system_manifest.h"

namespace framebeam::ui {

// Qt::Key -> RETRO_DEVICE_ID_JOYPAD-Maske; 0 = nicht belegt.
// Pfeile = Steuerkreuz, X=A, Z=B, S=X, A=Y, Q=L, W=R, Enter/Return=Start, Rücktaste=Select.
quint32 joypadMaskForKey(int qtKey);

// Haelt den Tastaturzustand (mehrere Tasten gleichzeitig). Auto-Repeat wird vom Aufrufer gefiltert.
class KeyboardJoypad {
 public:
  // true, wenn die Taste belegt ist (Ereignis wurde verbraucht).
  bool press(int qtKey);
  bool release(int qtKey);
  void clear() { mask_ = 0; }
  quint32 mask() const { return mask_; }

 private:
  quint32 mask_ = 0;
};

// Rechteck, in das ein Frame der Groesse `frame` in `area` passt (zentriert, Seitenverhaeltnis erhalten).
// integerScale: ganzzahliger Faktor, wenn mindestens 1x passt und der Platz damit gut genutzt wird
// (>= 75 % des moeglichen Faktors); sonst bruchteiliger Faktor (immer nearest-neighbour gezeichnet).
QRectF fitFrame(const QSizeF& frame, const QSizeF& area, bool integerScale);

// Item-Position -> normierte Position im Gesamtframe (EmulatorBackend::setPointer).
// frameRect: wo der Frame im Item liegt. clampToScreen=false: Positionen ausserhalb des Touch-Screens
// ergeben nullopt (Druckbeginn); true: auf den Screen begrenzt (gedrueckt halten und ziehen).
std::optional<QPointF> touchToFrame(const emu::DisplayProfile& profile, const QRectF& frameRect, const QPointF& itemPos,
                                    bool clampToScreen);

}  // namespace framebeam::ui
