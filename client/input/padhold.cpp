#include "padhold.h"

namespace uzume::input {

void PadHoldDetector::setModifier(const QString& token) {
  if (token == modifier_) return;
  modifier_ = token;
  reset();
}

void PadHoldDetector::reset() {
  heldSince_ = -1;
  blocked_ = false;
  visible_ = false;
}

bool PadHoldDetector::update(const QSet<QString>& pressed, qint64 nowMs) {
  if (modifier_.isEmpty() || !pressed.contains(modifier_)) {
    reset();
    return false;
  }
  const bool others = pressed.size() > 1;
  if (heldSince_ < 0) {  // the modifier just went down
    heldSince_ = nowMs;
    blocked_ = others;   // a button that is already held: not a "hold Select alone"
  } else if (others) {
    blocked_ = true;
  }
  if (blocked_) {
    visible_ = false;
  } else if (nowMs - heldSince_ >= holdMs_) {
    visible_ = true;
  }
  return visible_;
}

int PadHoldDetector::msUntilShow(qint64 nowMs) const {
  if (heldSince_ < 0 || blocked_ || visible_) return -1;
  const qint64 left = holdMs_ - (nowMs - heldSince_);
  return left > 0 ? static_cast<int>(left) : 0;
}

}  // namespace uzume::input
