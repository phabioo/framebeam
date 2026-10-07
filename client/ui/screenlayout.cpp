#include "screenlayout.h"

#include <algorithm>
#include <cmath>

#include "inputmapping.h"

namespace framebeam::ui {

QStringList screenLayoutsFor(int screenCount) {
  if (screenCount == 2) {
    return {QLatin1String(kLayoutStacked), QLatin1String(kLayoutSide), QLatin1String(kLayoutTop)};
  }
  return {};
}

bool isScreenLayout(const QString& layout) {
  return layout == QLatin1String(kLayoutStacked) || layout == QLatin1String(kLayoutSide) || layout == QLatin1String(kLayoutTop);
}

ScreenPlacement placeScreens(const emu::DisplayProfile& profile, const QString& layout, const QSizeF& area, bool integerScale) {
  ScreenPlacement out;
  const int n = static_cast<int>(profile.screens.size());
  out.targets = QList<QRectF>(n);
  if (n == 0) {
    return out;
  }
  const bool top = layout == QLatin1String(kLayoutTop);
  const bool side = layout == QLatin1String(kLayoutSide);

  // Logical positions in profile pixels.
  QList<QRectF> logical(n);
  qreal maxW = 0, maxH = 0;
  const int shown = top ? 1 : n;
  for (int i = 0; i < shown; ++i) {
    maxW = std::max<qreal>(maxW, profile.screens.at(i).width);
    maxH = std::max<qreal>(maxH, profile.screens.at(i).height);
  }
  qreal x = 0, y = 0;
  qreal totalW = 0, totalH = 0;
  for (int i = 0; i < shown; ++i) {
    const qreal w = profile.screens.at(i).width, h = profile.screens.at(i).height;
    const qreal gap = i > 0 ? profile.gap : 0;
    if (side) {
      x += gap;
      logical[i] = QRectF(x, (maxH - h) / 2, w, h);
      x += w;
      totalW = x;
      totalH = maxH;
    } else if (top) {
      logical[i] = QRectF(0, 0, w, h);
      totalW = w;
      totalH = h;
    } else {  // stacked: a column, centered
      y += gap;
      logical[i] = QRectF((maxW - w) / 2, y, w, h);
      y += h;
      totalW = maxW;
      totalH = y;
    }
  }
  const QRectF fit = fitFrame(QSizeF(totalW, totalH), area, integerScale);
  if (fit.isEmpty()) {
    return out;
  }
  const qreal scale = fit.width() / totalW;
  for (int i = 0; i < shown; ++i) {
    const QRectF& l = logical.at(i);
    out.targets[i] = QRectF(fit.x() + l.x() * scale, fit.y() + l.y() * scale, l.width() * scale, l.height() * scale);
  }
  out.content = fit;
  return out;
}

QRectF screenSourceRect(const emu::DisplayProfile& profile, const QSizeF& frame, int index) {
  const QSize base = profile.frameSize();
  const QRect r = profile.screenRect(index);
  if (base.isEmpty() || r.isEmpty() || frame.isEmpty()) {
    return {};
  }
  const qreal sx = frame.width() / base.width(), sy = frame.height() / base.height();
  return QRectF(r.x() * sx, r.y() * sy, r.width() * sx, r.height() * sy);
}

std::optional<QPointF> touchToFrameFor(const emu::DisplayProfile& profile, const ScreenPlacement& placement, const QPointF& itemPos,
                                       bool clampToScreen) {
  const int idx = profile.touchScreenIndex();
  if (idx < 0 || idx >= placement.targets.size()) {
    return std::nullopt;
  }
  const QRectF t = placement.targets.at(idx);
  if (t.isEmpty()) {
    return std::nullopt;
  }
  QPointF n((itemPos.x() - t.x()) / t.width(), (itemPos.y() - t.y()) / t.height());
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

emu::DisplayProfile profileForRemoteFrame(const QSizeF& frame) {
  emu::DisplayProfile p;
  const int w = static_cast<int>(std::lround(frame.width())), h = static_cast<int>(std::lround(frame.height()));
  if (w > 0 && h >= w * 6 / 5 && h % 2 == 0) {
    p.layout = QStringLiteral("vertical");
    p.screens = {{QStringLiteral("top"), w, h / 2, false}, {QStringLiteral("bottom"), w, h / 2, false}};
  } else {
    p.layout = QStringLiteral("single");
    p.screens = {{QStringLiteral("screen"), std::max(w, 1), std::max(h, 1), false}};
  }
  return p;
}

}  // namespace framebeam::ui
