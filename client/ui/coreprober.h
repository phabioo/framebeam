// Runs core probes (dlopen, core info, options; core_options.h) off the GUI thread.
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <memory>

#include "core_options.h"

namespace framebeam::ui {

// Only one libretro core can be loaded per process, so all probes run one after another on a single worker thread.
// A library that is already queued or running is not probed a second time: its requesters are served by that probe.
// Results arrive on the owner's thread via `probed`; `idle` follows the last one.
class CoreProber : public QObject {
  Q_OBJECT

 public:
  explicit CoreProber(QObject* parent = nullptr);
  ~CoreProber() override;

  // Queues a probe of `corePath` for `coreId`. Returns false when that library is already queued/running
  // (the result is still delivered for this `coreId`).
  bool request(const QString& coreId, const QString& corePath, const QString& systemDir, const QString& probeDir);
  bool busy() const { return !pending_.isEmpty(); }
  bool pending(const QString& corePath) const { return pending_.contains(corePath); }

  // Test hook: probe function (default emu::probeCoreGuarded); runs on the worker thread.
  using ProbeFn = emu::CoreProbe (*)(const QString&, const QString&, const QString&);
  static void setProbeFunctionForTest(ProbeFn fn);

 signals:
  void probed(const QString& coreId, const QString& corePath, const framebeam::emu::CoreProbe& probe);
  void idle();

 private:
  void deliver(const QString& corePath, const emu::CoreProbe& probe);

  std::unique_ptr<QThreadPool> pool_;  // leaked on quit when a probe hangs (the pool destructor waits without limit)
  QHash<QString, QStringList> pending_;  // core path -> core ids waiting for its result
};

}  // namespace framebeam::ui
