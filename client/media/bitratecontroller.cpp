#include "bitratecontroller.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace framebeam {

namespace {
// Decoder names are libavcodec names ("h264", "h264_cuvid"): short, no spaces; anything else is not shown.
bool validDecoderName(const QString& n) {
  if (n.isEmpty() || n.size() > 32) return false;
  for (const QChar c : n) {
    if (!(c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-') || c == QLatin1Char('.'))) return false;
  }
  return true;
}
}  // namespace

QByteArray makeRxReport(double loss, double kbps, double fps, const QString& decoder) {
  QJsonObject o{{QStringLiteral("t"), QStringLiteral("rx")},
                {QStringLiteral("loss"), std::round(std::clamp(loss, 0.0, 1.0) * 10000.0) / 10000.0},
                {QStringLiteral("kbps"), std::round(std::max(0.0, kbps))}};
  if (fps >= 0.0 && std::isfinite(fps)) {
    o.insert(QStringLiteral("fps"), std::round(std::min(fps, 1000.0) * 10.0) / 10.0);
  }
  if (validDecoderName(decoder)) {
    o.insert(QStringLiteral("dec"), decoder);
  }
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<RxReport> parseRxReport(const QByteArray& message) {
  if (message.isEmpty() || message.size() > 512) {
    return std::nullopt;
  }
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(message, &pe);
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
    return std::nullopt;
  }
  const QJsonObject o = doc.object();
  if (o.value(QStringLiteral("t")).toString() != QLatin1String("rx")) {
    return std::nullopt;
  }
  const QJsonValue loss = o.value(QStringLiteral("loss")), kbps = o.value(QStringLiteral("kbps"));
  if (!loss.isDouble() || !kbps.isDouble()) {
    return std::nullopt;
  }
  RxReport r;
  r.loss = loss.toDouble();
  r.kbps = kbps.toDouble();
  if (!std::isfinite(r.loss) || !std::isfinite(r.kbps) || r.loss < 0.0 || r.loss > 1.0 || r.kbps < 0.0) {
    return std::nullopt;
  }
  const QJsonValue fps = o.value(QStringLiteral("fps")), dec = o.value(QStringLiteral("dec"));
  if (fps.isDouble() && std::isfinite(fps.toDouble()) && fps.toDouble() >= 0.0 && fps.toDouble() <= 1000.0) {
    r.fps = fps.toDouble();
  }
  if (dec.isString() && validDecoderName(dec.toString())) {
    r.decoder = dec.toString();
  }
  return r;
}

void BitrateController::reset(int startKbps) {
  target_ = clampKbps(startKbps < 0 ? config_.startKbps : startKbps);
  viewers_.clear();
  lastChangeMs_.reset();
}

std::optional<int> BitrateController::report(const QString& viewerId, const RxReport& r, qint64 nowMs) {
  ViewerState& v = viewers_[viewerId];
  v.loss = r.loss;
  v.goodStreak = r.loss < config_.lowLoss ? v.goodStreak + 1 : 0;

  const bool intervalOk = !lastChangeMs_ || nowMs - *lastChangeMs_ >= config_.minChangeIntervalMs;
  if (!intervalOk) {
    return std::nullopt;  // state is kept; the next report decides again
  }

  double worstLoss = 0.0;
  bool allGood = true;
  for (const ViewerState& s : std::as_const(viewers_)) {
    worstLoss = std::max(worstLoss, s.loss);
    allGood = allGood && s.goodStreak >= config_.goodReportsForIncrease;
  }

  int next = target_;
  if (worstLoss > config_.highLoss) {
    next = clampKbps(static_cast<int>(std::lround(target_ * config_.decreaseFactor)));
  } else if (allGood) {
    next = clampKbps(static_cast<int>(std::lround(target_ * config_.increaseFactor)));
    if (next == target_ && target_ < config_.maxKbps) {
      next = std::min(config_.maxKbps, target_ + 1);  // rounding never stalls below the maximum
    }
  }
  if (next == target_) {
    return std::nullopt;
  }
  target_ = next;
  lastChangeMs_ = nowMs;
  for (ViewerState& s : viewers_) {
    s.goodStreak = 0;  // a new level has to prove itself with three clean reports
  }
  return target_;
}

}  // namespace framebeam
