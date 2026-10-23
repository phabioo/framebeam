#include "screenlayout.h"

#include <algorithm>
#include <QCoreApplication>
#include <cmath>

#include "inputmapping.h"

namespace uzume::ui {

QStringList screenLayoutsFor(int screenCount) {
  if (screenCount == 2) {
    return {QLatin1String(kLayoutStacked), QLatin1String(kLayoutSide), QLatin1String(kLayoutLarge), QLatin1String(kLayoutTop),
            QLatin1String(kLayoutBottom)};
  }
  return {};
}

bool isScreenLayout(const QString& layout) {
  return layout == QLatin1String(kLayoutStacked) || layout == QLatin1String(kLayoutSide) || layout == QLatin1String(kLayoutLarge) ||
         layout == QLatin1String(kLayoutTop) || layout == QLatin1String(kLayoutBottom);
}

bool layoutSupportsSwap(const QString& layout) {
  return layout == QLatin1String(kLayoutStacked) || layout == QLatin1String(kLayoutSide) || layout == QLatin1String(kLayoutLarge);
}

QString screenLayoutLabel(const QString& layout) {
  if (layout == QLatin1String(kLayoutSide)) return QCoreApplication::translate("ScreenLayout", "Side by side");
  if (layout == QLatin1String(kLayoutLarge)) return QCoreApplication::translate("ScreenLayout", "Large top / small bottom");
  if (layout == QLatin1String(kLayoutTop)) return QCoreApplication::translate("ScreenLayout", "Top screen only");
  if (layout == QLatin1String(kLayoutBottom)) return QCoreApplication::translate("ScreenLayout", "Bottom screen only");
  return QCoreApplication::translate("ScreenLayout", "Stacked");
}

QString nextScreenLayout(const QStringList& layouts, const QString& current) {
  if (layouts.isEmpty()) return {};
  const int i = layouts.indexOf(current);
  return layouts.at((i + 1) % layouts.size());  // unknown (-1) -> index 0
}

ScreenPlacement placeScreens(const emu::DisplayProfile& profile, const QString& layout, const QSizeF& area, bool integerScale,
                             bool strictInteger, AspectMode aspect, bool swap) {
  ScreenPlacement out;
  const int n = static_cast<int>(profile.screens.size());
  out.targets = QList<QRectF>(n);
  if (n == 0) {
    return out;
  }
  const bool top = layout == QLatin1String(kLayoutTop);
  const bool bottom = layout == QLatin1String(kLayoutBottom) && n >= 2;
  const bool side = layout == QLatin1String(kLayoutSide);
  const bool large = layout == QLatin1String(kLayoutLarge) && n >= 2;

  // Drawing order: the screens that are shown, first one first. Swap reverses it for the layouts that show both.
  QList<int> order;
  if (top) {
    order = {0};
  } else if (bottom) {
    order = {n - 1};
  } else {
    for (int i = 0; i < n; ++i) order.append(i);
    if (swap) std::reverse(order.begin(), order.end());
  }

  // Logical positions in profile pixels.
  QList<QRectF> logical(n);
  // 4:3: every screen keeps its height and gets the width of a 4:3 screen.
  auto screenW = [&](int i) -> qreal {
    return aspect == AspectMode::Ratio43 ? profile.screens.at(i).height * 4.0 / 3.0 : profile.screens.at(i).width;
  };
  qreal maxW = 0, maxH = 0;
  for (int i : order) {
    maxW = std::max<qreal>(maxW, screenW(i));
    maxH = std::max<qreal>(maxH, profile.screens.at(i).height);
  }
  qreal x = 0, y = 0;
  qreal totalW = 0, totalH = 0;
  if (large) {
    // The first screen of the order at 2x, the second at 1x on its right edge, both bottom-aligned.
    const int big = order.at(0), small = order.at(1);
    const qreal bw = screenW(big) * 2, bh = profile.screens.at(big).height * 2;
    const qreal sw = screenW(small), sh = profile.screens.at(small).height;
    logical[big] = QRectF(0, bh >= sh ? 0 : sh - bh, bw, bh);
    totalH = std::max(bh, sh);
    logical[small] = QRectF(bw + profile.gap, totalH - sh, sw, sh);
    totalW = bw + profile.gap + sw;
  } else {
    bool first = true;
    for (int i : order) {
      const qreal w = screenW(i), h = profile.screens.at(i).height;
      const qreal gap = first ? 0 : profile.gap;
      first = false;
      if (side) {
        x += gap;
        logical[i] = QRectF(x, (maxH - h) / 2, w, h);
        x += w;
        totalW = x;
        totalH = maxH;
      } else if (top || bottom) {
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
  }
  const QRectF fit = aspect == AspectMode::Stretch ? fitPicture(QSizeF(totalW, totalH), area, integerScale, strictInteger, aspect)
                                                   : fitFrame(QSizeF(totalW, totalH), area, integerScale, strictInteger);
  if (fit.isEmpty()) {
    return out;
  }
  const qreal scaleX = fit.width() / totalW, scaleY = fit.height() / totalH;
  for (int i : order) {
    const QRectF& l = logical.at(i);
    out.targets[i] = QRectF(fit.x() + l.x() * scaleX, fit.y() + l.y() * scaleY, l.width() * scaleX, l.height() * scaleY);
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

}  // namespace uzume::ui
