#include "coreprober.h"

#include <QDebug>
#include <QMetaObject>
#include <QMetaType>
#include <atomic>

namespace uzume::ui {

namespace {
std::atomic<CoreProber::ProbeFn> g_probeFn{&emu::probeCoreGuarded};
}

void CoreProber::setProbeFunctionForTest(ProbeFn fn) { g_probeFn.store(fn != nullptr ? fn : &emu::probeCoreGuarded); }

CoreProber::CoreProber(QObject* parent) : QObject(parent) {
  pool_ = std::make_unique<QThreadPool>();
  pool_->setMaxThreadCount(1);
  qRegisterMetaType<uzume::emu::CoreProbe>("uzume::emu::CoreProbe");
}

CoreProber::~CoreProber() {
  if (!pool_->waitForDone(10000)) {
    qWarning() << "A core probe did not finish within 10 s; not waiting any longer";
    (void)pool_.release();
  }
}

bool CoreProber::request(const QString& coreId, const QString& corePath, const QString& systemDir, const QString& probeDir) {
  const auto it = pending_.find(corePath);
  if (it != pending_.end()) {
    if (!it->contains(coreId)) it->append(coreId);
    return false;
  }
  pending_.insert(corePath, {coreId});
  QPointer<CoreProber> self(this);
  pool_->start([self, corePath, systemDir, probeDir]() {
    const emu::CoreProbe probe = g_probeFn.load()(corePath, systemDir, probeDir);
    if (self == nullptr) return;
    QMetaObject::invokeMethod(
        self.data(),
        [self, corePath, probe]() {
          if (self != nullptr) self->deliver(corePath, probe);
        },
        Qt::QueuedConnection);
  });
  return true;
}

void CoreProber::deliver(const QString& corePath, const emu::CoreProbe& probe) {
  const QStringList ids = pending_.take(corePath);
  for (const QString& id : ids) emit probed(id, corePath, probe);
  if (pending_.isEmpty()) emit idle();
}

}  // namespace uzume::ui
