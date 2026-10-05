#include "inputmapping.h"

#include <QtMath>
#include <algorithm>

#include "emulator_backend.h"

namespace framebeam::ui {

quint32 joypadMaskForKey(int qtKey) {
  using emu::JoypadButton;
  using emu::buttonMask;
  switch (qtKey) {
    case Qt::Key_Up: return buttonMask(JoypadButton::Up);
    case Qt::Key_Down: return buttonMask(JoypadButton::Down);
    case Qt::Key_Left: return buttonMask(JoypadButton::Left);
    case Qt::Key_Right: return buttonMask(JoypadButton::Right);
    case Qt::Key_X: return buttonMask(JoypadButton::A);
    case Qt::Key_Z: return buttonMask(JoypadButton::B);
    case Qt::Key_S: return buttonMask(JoypadButton::X);
    case Qt::Key_A: return buttonMask(JoypadButton::Y);
    case Qt::Key_Q: return buttonMask(JoypadButton::L);
    case Qt::Key_W: return buttonMask(JoypadButton::R);
    case Qt::Key_Return:
    case Qt::Key_Enter: return buttonMask(JoypadButton::Start);
    case Qt::Key_Backspace: return buttonMask(JoypadButton::Select);
    default: return 0;
  }
}

bool KeyboardJoypad::press(int qtKey) {
  const quint32 m = joypadMaskForKey(qtKey);
  mask_ |= m;
  return m != 0;
}

bool KeyboardJoypad::release(int qtKey) {
  const quint32 m = joypadMaskForKey(qtKey);
  mask_ &= ~m;
  return m != 0;
}

QRectF fitFrame(const QSizeF& frame, const QSizeF& area, bool integerScale) {
  if (frame.width() <= 0 || frame.height() <= 0 || area.width() <= 0 || area.height() <= 0) {
    return {};
  }
  qreal scale = std::min(area.width() / frame.width(), area.height() / frame.height());
  if (integerScale && scale >= 1.0) {
    // Integer scale as long as the image still uses the space well (>= 75 % of the possible factor);
    // otherwise, e.g., 1x in a window almost 2x the size would leave a large empty area.
    const qreal whole = std::floor(scale);
    if (whole / scale >= 0.75) {
      scale = whole;
    }
  }
  const qreal w = std::round(frame.width() * scale);
  const qreal h = std::round(frame.height() * scale);
  return QRectF(std::round((area.width() - w) / 2.0), std::round((area.height() - h) / 2.0), w, h);
}

std::optional<QPointF> touchToFrame(const emu::DisplayProfile& profile, const QRectF& frameRect, const QPointF& itemPos,
                                    bool clampToScreen) {
  const int idx = profile.touchScreenIndex();
  const QSize fs = profile.frameSize();
  if (idx < 0 || fs.isEmpty() || frameRect.isEmpty()) {
    return std::nullopt;
  }
  const QRect sr = profile.screenRect(idx);
  // Screen rectangle in item coordinates.
  const qreal sx = frameRect.width() / fs.width();
  const qreal sy = frameRect.height() / fs.height();
  const QRectF screen(frameRect.x() + sr.x() * sx, frameRect.y() + sr.y() * sy, sr.width() * sx, sr.height() * sy);
  if (screen.isEmpty()) {
    return std::nullopt;
  }
  QPointF n((itemPos.x() - screen.x()) / screen.width(), (itemPos.y() - screen.y()) / screen.height());
  const bool inside = n.x() >= 0.0 && n.x() <= 1.0 && n.y() >= 0.0 && n.y() <= 1.0;
  if (!inside) {
    if (!clampToScreen) {
      return std::nullopt;
    }
    n.setX(std::clamp(n.x(), 0.0, 1.0));
    n.setY(std::clamp(n.y(), 0.0, 1.0));
  }
  return profile.toFrameNormalized(idx, n);
}

}  // namespace framebeam::ui
