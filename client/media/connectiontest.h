#pragma once
// Step 5 of the connection check: two WebRTC peers inside this Player (libdatachannel), configured with the ICE servers the Hub
// handed out, connect to each other through in-process signaling and echo a few messages over a data channel.
//
// What it proves: the Player's WebRTC stack works with these servers; the number of host / server-reflexive / relayed candidates it
// gathers (srflx > 0 means a STUN server answered through the real ICE agent); with `relayOnly` the ICE policy lets only TURN
// candidates through, so a successful echo means data really flows through the Hub's relay (allocation, permission and relaying
// all work), with the measured round-trip time. What it does NOT prove: that a direct path to another Player works, which also
// depends on that Player's network. The Hub has no WebRTC peer to test against.

#include <memory>

#include "connectionprobes.h"

namespace framebeam {

// Real probes: the Qt probes of steps 1 to 3 plus this test as step 5. Create and use on the thread that runs the ConnectionCheck.
std::unique_ptr<ConnectionProbes> makeConnectionProbes(const ProbeTimings& timings = {});

// The test alone (tests, other callers). The callback comes on the thread that created the runner.
class ConnectionTestRunner : public QObject {
  Q_OBJECT
 public:
  explicit ConnectionTestRunner(QObject* parent = nullptr);
  ~ConnectionTestRunner() override;
  void run(const ConnectionTarget& target, const TestPlan& plan, std::function<void(const TestProbeResult&)> done);
  void cancel();
  void setTimeoutMs(int ms) { timeoutMs_ = ms; }

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
  int timeoutMs_ = 15000;
};

}  // namespace framebeam
