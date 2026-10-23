#pragma once
// PadHoldDetector: pure (no SDL, no timers) detection of "the hotkey modifier (default Select) is held alone" for the
// hold-Select hotkey sheet. The caller feeds the pressed tokens of P1 with a monotonic millisecond clock whenever they
// change and again when msUntilShow() has elapsed. It never touches PadHotkeyDetector: combos fire and the modifier
// reaches the core exactly as before.
//
// The sheet shows once the modifier has been held alone for holdMs. Any other button pressed while the modifier is down
// (before or after it) hides the sheet and keeps it hidden until the modifier is released, so a combo that fires
// (Select + R) or a button that was already held never flashes the sheet.

#include <QSet>
#include <QString>
#include <QtGlobal>

namespace uzume::input {

class PadHoldDetector {
 public:
  static constexpr int kDefaultHoldMs = 400;

  void setModifier(const QString& token);
  void setHoldMs(int ms) { holdMs_ = ms < 0 ? 0 : ms; }
  // Feeds the pressed tokens of P1; returns visible().
  bool update(const QSet<QString>& pressed, qint64 nowMs);
  bool visible() const { return visible_; }
  // Milliseconds until the sheet would show if nothing changes; -1 = no timer needed (not held, blocked or already shown).
  int msUntilShow(qint64 nowMs) const;
  void reset();

 private:
  QString modifier_;
  int holdMs_ = kDefaultHoldMs;
  qint64 heldSince_ = -1;
  bool blocked_ = false;
  bool visible_ = false;
};

}  // namespace uzume::input
