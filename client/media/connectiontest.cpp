#include "connectiontest.h"

#include <QMetaObject>
#include <QPointer>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <variant>

#include "rtcutil.h"

namespace uzume {

struct ConnectionTestRunner::Impl {
  std::mutex m;
  std::condition_variable cv;
  std::atomic<bool> cancelled{false};
  std::thread worker;

  void stop() {
    cancelled = true;
    cv.notify_all();
    if (worker.joinable()) worker.join();
  }
};

namespace {

struct Shared {
  std::mutex m;
  std::condition_variable cv;
  bool open = false;
  bool failed = false;
  std::string failure;
  std::vector<std::chrono::steady_clock::time_point> sent;
  std::vector<double> rttMs;
  int pings = 0;
  CandidateCounts local;
  std::shared_ptr<rtc::DataChannel> dcB;
};

constexpr int kPings = 5;

TestProbeResult runTest(const ConnectionTarget& target, const TestPlan& plan, int timeoutMs, std::atomic<bool>& cancelled) {
  TestProbeResult out;
  auto sh = std::make_shared<Shared>();
  std::shared_ptr<rtc::PeerConnection> a, b;
  std::shared_ptr<rtc::DataChannel> dcA;
  try {
    const rtc::Configuration cfg = makeRtcConfig(target.stunUrls, target.turnServers, plan.relayOnly);
    a = std::make_shared<rtc::PeerConnection>(cfg);
    b = std::make_shared<rtc::PeerConnection>(cfg);
    // In-process signaling. The peers are held by this function until the end, the weak pointers keep late callbacks harmless.
    std::weak_ptr<rtc::PeerConnection> wa = a, wb = b;
    a->onLocalDescription([wb](rtc::Description d) { if (auto p = wb.lock()) p->setRemoteDescription(d); });
    b->onLocalDescription([wa](rtc::Description d) { if (auto p = wa.lock()) p->setRemoteDescription(d); });
    a->onLocalCandidate([wb, sh](rtc::Candidate c) {
      { std::lock_guard<std::mutex> l(sh->m); countCandidate(sh->local, c.type()); }
      if (auto p = wb.lock()) p->addRemoteCandidate(c);
    });
    b->onLocalCandidate([wa](rtc::Candidate c) { if (auto p = wa.lock()) p->addRemoteCandidate(c); });
    const auto fail = [sh](const char* why) {
      std::lock_guard<std::mutex> l(sh->m);
      if (!sh->failed) { sh->failed = true; sh->failure = why; }
      sh->cv.notify_all();
    };
    a->onStateChange([fail](rtc::PeerConnection::State s) { if (s == rtc::PeerConnection::State::Failed) fail("the peers could not establish a connection"); });
    b->onStateChange([fail](rtc::PeerConnection::State s) { if (s == rtc::PeerConnection::State::Failed) fail("the peers could not establish a connection"); });

    b->onDataChannel([sh](std::shared_ptr<rtc::DataChannel> dc) {
      { std::lock_guard<std::mutex> l(sh->m); sh->dcB = dc; }
      std::weak_ptr<rtc::DataChannel> w = dc;
      dc->onMessage([w](rtc::message_variant msg) {
        if (auto p = w.lock()) {
          if (std::holds_alternative<std::string>(msg)) p->send(std::get<std::string>(msg));
        }
      });
    });
    dcA = a->createDataChannel("fb-check");
    std::weak_ptr<rtc::DataChannel> wdc = dcA;
    const auto sendPing = [sh, wdc]() {
      if (auto p = wdc.lock()) {
        {
          std::lock_guard<std::mutex> l(sh->m);
          sh->sent.push_back(std::chrono::steady_clock::now());
        }
        p->send(std::string("ping"));
      }
    };
    dcA->onOpen([sh, sendPing]() {
      { std::lock_guard<std::mutex> l(sh->m); sh->open = true; }
      sendPing();
    });
    dcA->onMessage([sh, sendPing](rtc::message_variant) {
      bool more = false;
      {
        std::lock_guard<std::mutex> l(sh->m);
        const auto now = std::chrono::steady_clock::now();
        sh->rttMs.push_back(std::chrono::duration<double, std::milli>(now - sh->sent.back()).count());
        ++sh->pings;
        more = sh->pings < kPings;
        if (!more) sh->cv.notify_all();
      }
      if (more) sendPing();
    });

    {
      std::unique_lock<std::mutex> l(sh->m);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
      while (!(sh->pings >= kPings || sh->failed || cancelled)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        sh->cv.wait_for(l, std::chrono::milliseconds(100));  // short slices: cancel() does not signal this condition variable
      }
      out.hostCandidates = sh->local.host;
      out.srflxCandidates = sh->local.srflx;
      out.relayCandidates = sh->local.relay;
      if (sh->pings >= kPings) {
        out.ok = true;
        double best = sh->rttMs.empty() ? 0 : sh->rttMs.front();
        for (double v : sh->rttMs) best = std::min(best, v);
        out.rttMs = std::max(1, static_cast<int>(best + 0.999));
      } else if (sh->failed) {
        out.error = QString::fromStdString(sh->failure);
      } else if (!cancelled) {
        out.error = sh->open ? QStringLiteral("the data channel did not answer in time") : QStringLiteral("no connection within %1 s").arg(timeoutMs / 1000);
      }
    }
    if (out.ok) {
      rtc::Candidate local, remote;
      if (a->getSelectedCandidatePair(&local, &remote)) {
        const QString t = connectionTypeOfCandidates(local, remote, false, plan.relayOnly);
        out.pathType = t.startsWith(QLatin1String("relay")) ? QStringLiteral("relay") : t.contains(QLatin1String("srflx")) ? QStringLiteral("srflx")
                       : t.contains(QLatin1String("prflx")) ? QStringLiteral("prflx") : QStringLiteral("host");
      }
      if (plan.relayOnly && out.pathType != QLatin1String("relay")) {
        out.ok = false;
        out.error = QStringLiteral("data flowed, but not through the relay");
      }
    }
    try { dcA->close(); } catch (...) {}
    try { a->close(); b->close(); } catch (...) {}
    { std::lock_guard<std::mutex> l(sh->m); sh->dcB.reset(); }
  } catch (const std::exception& e) {
    out.ok = false;
    out.error = QString::fromUtf8(e.what());
  }
  return out;
}

}  // namespace

ConnectionTestRunner::ConnectionTestRunner(QObject* parent) : QObject(parent), impl_(std::make_shared<Impl>()) {}

ConnectionTestRunner::~ConnectionTestRunner() { impl_->stop(); }

void ConnectionTestRunner::cancel() { impl_->stop(); }

void ConnectionTestRunner::run(const ConnectionTarget& target, const TestPlan& plan, std::function<void(const TestProbeResult&)> done) {
  impl_->stop();
  impl_->cancelled = false;
  const int timeoutMs = timeoutMs_;
  std::shared_ptr<Impl> impl = impl_;
  QObject* const me = this;  // alive while the thread runs: the destructor joins it
  impl_->worker = std::thread([me, impl, target, plan, timeoutMs, done = std::move(done)]() mutable {
    TestProbeResult r = runTest(target, plan, timeoutMs, impl->cancelled);
    if (impl->cancelled) return;  // canceled: nobody waits for the answer
    QMetaObject::invokeMethod(me, [done = std::move(done), r]() { done(r); }, Qt::QueuedConnection);
  });
}

std::unique_ptr<ConnectionProbes> makeConnectionProbes(const ProbeTimings& timings) {
  auto runner = std::make_shared<ConnectionTestRunner>();
  ConnectionTestFn test = [runner](const ConnectionTarget& t, const TestPlan& p, std::function<void(const TestProbeResult&)> done) {
    runner->run(t, p, std::move(done));
  };
  ConnectionTestCancelFn cancel = [runner]() { runner->cancel(); };
  return makeQtConnectionProbes(std::move(test), std::move(cancel), timings);
}

}  // namespace uzume
