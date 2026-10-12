#include "padhotkeys.h"

namespace framebeam::input {

void PadHotkeyDetector::setBindings(const QString& modifierToken, const QList<PadHotkeyBinding>& bindings) {
  modifier_ = modifierToken;
  bindings_ = bindings;
}

void PadHotkeyDetector::reset() {
  prev_.clear();
  active_.clear();
  suppressed_.clear();
}

QStringList PadHotkeyDetector::update(const QSet<QString>& pressed) {
  QStringList fired;
  const bool modHeld = !modifier_.isEmpty() && pressed.contains(modifier_);
  for (const PadHotkeyBinding& b : std::as_const(bindings_)) {
    if (b.token.isEmpty() || b.token == modifier_) continue;
    const bool down = pressed.contains(b.token);
    if (down && !prev_.contains(b.token) && (b.solo || modHeld)) {
      fired.append(b.action);
      if (!b.solo) active_.insert(b.token);
    }
  }
  active_.intersect(pressed);  // released: visible again
  suppressed_ = active_;
  for (const PadHotkeyBinding& b : std::as_const(bindings_)) {
    if (b.solo && !b.token.isEmpty() && b.token != modifier_ && pressed.contains(b.token)) suppressed_.insert(b.token);
  }
  prev_ = pressed;
  return fired;
}

}  // namespace framebeam::input
