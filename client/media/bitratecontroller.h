#pragma once

// Bitrate adaptation of the Session encoder (ADR 0012 D5): pure AIMD logic, no Qt event loop, no clock of its own.

#include <QByteArray>
#include <QHash>
#include <QString>
#include <optional>

namespace framebeam {

// One viewer report over the fb-diag DataChannel: {"t":"rx","loss":<0..1>,"kbps":<received video kbit/s>} plus, since
// 0.6, the optional "fps" (frames/s the viewer decoded) and "dec" (name of its decoder). Old Players send neither and
// are read as before; invalid optional values are dropped, the report itself stays valid.
struct RxReport {
  double loss = 0.0;  // 0..1, packets lost in the last second
  double kbps = 0.0;  // video kbit/s received in the last second
  std::optional<double> fps;
  QString decoder;
};
// fps < 0 / empty decoder: field left out.
QByteArray makeRxReport(double loss, double kbps, double fps = -1.0, const QString& decoder = QString());
// nullopt for anything that is not a well-formed rx report (other message types, bad JSON, wrong types, NaN,
// loss outside 0..1, negative kbps): the host ignores those.
std::optional<RxReport> parseRxReport(const QByteArray& message);

// AIMD over the worst viewer: start 2000 kbit/s; a report above 5 % loss multiplies the target by 0.7 (minimum
// 300); three consecutive reports below 1 % of every viewer add 10 % (maximum 4000); at most one change per 2 s.
// Every viewer's latest report counts; a viewer that leaves is removed with removeViewer().
class BitrateController {
 public:
  struct Config {
    int startKbps = 2000;
    int minKbps = 300;
    int maxKbps = 4000;
    double highLoss = 0.05;
    double lowLoss = 0.01;
    int goodReportsForIncrease = 3;
    double decreaseFactor = 0.7;
    double increaseFactor = 1.1;
    qint64 minChangeIntervalMs = 2000;
  };

  BitrateController() = default;
  explicit BitrateController(const Config& config) : config_(config), target_(clampKbps(config.startKbps)) {}

  int targetKbps() const { return target_; }

  // Feeds one report of `viewerId` received at `nowMs` (any monotonic clock). Returns the new target if it changed.
  std::optional<int> report(const QString& viewerId, const RxReport& r, qint64 nowMs);
  void removeViewer(const QString& viewerId) { viewers_.remove(viewerId); }
  // Back to the start value without viewers (new Session).
  void reset(int startKbps = -1);

 private:
  struct ViewerState {
    double loss = 0.0;
    int goodStreak = 0;  // consecutive reports below lowLoss
  };
  int clampKbps(int v) const { return v < config_.minKbps ? config_.minKbps : (v > config_.maxKbps ? config_.maxKbps : v); }

  Config config_;
  int target_ = config_.startKbps;
  QHash<QString, ViewerState> viewers_;
  std::optional<qint64> lastChangeMs_;
};

}  // namespace framebeam
