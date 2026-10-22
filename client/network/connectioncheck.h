#pragma once
// Connection check of the Player (design 6e): five steps that tell a user in plain language whether Sessions can run
// directly between Players, only through the Hub's relay, or not at all from here.
//
//   1 Hub reachable    HTTPS info endpoint of the active Hub (pinned certificate), latency
//   2 STUN             binding requests to the STUN servers the Hub hands out; public address
//   3 TURN relay       allocation on the Hub's TURN with the handed short-lived credentials (skipped when the Hub has TURN off)
//   4 NAT type         the STUN mappings of two servers compared: same mapping = endpoint-independent, different = symmetric
//   5 Test connection  two WebRTC peers inside this Player (libdatachannel) with the Hub's ICE servers: relay-only through the
//                      Hub's TURN when the network needs it, otherwise a normal loopback that must gather a public candidate.
//                      It proves that the Player's WebRTC stack works with these servers and, relay-only, that data really flows
//                      through the relay. It does NOT prove that a direct path to another Player works: that depends on the other
//                      side's NAT and cannot be tested alone (rows 2 and 4 are the estimate).
//
// ConnectionCheck is the pure sequencing and decision logic. The network calls sit behind ConnectionProbes (Qt implementation in
// connectionprobes.h, WebRTC test in media/connectiontest.h), so tests drive it with fakes. It lives on one thread (the UI
// moves it to a worker thread); probes call their callback exactly once, from that thread, synchronously or later.

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>

#include "sessiontypes.h"

namespace framebeam {

enum class CheckStepId { Hub = 0, Stun, Turn, Nat, Test };
inline constexpr int kCheckStepCount = 5;
enum class CheckStepState { Waiting, Running, Ok, Warning, Failed, Skipped };
enum class CheckOutcome {
  Running,         // not finished
  Direct,          // direct connections possible
  Relayed,         // only through the relay
  Blocked,         // neither direct nor relayed (or no way across networks configured)
  HubUnreachable,  // step 1 failed, the rest skipped
  NotConnected,    // the Hub answers, but this Player has no Hub session, so the Hub's STUN/TURN servers are unknown
  Canceled,
  TimedOut
};
enum class NatType { Unknown, None, EndpointIndependent, Symmetric };

QString stepIdName(CheckStepId id);        // "hub", "stun", "turn", "nat", "test"
QString stepTitle(CheckStepId id);         // "Hub reachable", "STUN", ...
QString stepStateName(CheckStepState s);   // "waiting", "running", "ok", "warning", "failed", "skipped"
QString outcomeName(CheckOutcome o);       
QString natTypeName(NatType t);            // "unknown", "none", "endpoint_independent", "symmetric"

// What is checked: the active Hub and the ICE servers it handed out (hello_ack / ice_update).
struct ConnectionTarget {
  QString hubName;
  QString hubAddress;  // scheme://host:port, as stored in the Hub profile
  QString pinnedFingerprint;
  bool allowHttp = false;
  QString deviceName;
  QStringList stunUrls;  // stun: URLs
  QList<TurnServer> turnServers;
  bool iceKnown = true;     // the Hub session is open, so stunUrls / turnServers are what the Hub hands out (empty = none)
  bool forceRelay = false;  // FRAMEBEAM_FORCE_RELAY on this Player (ADR 0012)
};

struct HubProbeResult {
  bool ok = false;
  int latencyMs = 0;
  QString error;      // technical reason, no secrets
  QString errorKind;  // "timeout", "refused", "dns", "certificate", "http", "other"
};

struct StunMapping {
  QString server;    // host:port asked
  QString mapped;    // ip:port the server saw (XOR-MAPPED-ADDRESS); empty = no answer
  QString local;     // ip:port of the local socket the request left from (same socket for all servers)
  int rttMs = 0;
  bool answered() const { return !mapped.isEmpty(); }
  bool behindNat = true;  // false when `mapped` is an address of a local interface
};
struct StunProbeResult {
  QList<StunMapping> mappings;  // one per asked server, in the order asked
};

struct TurnProbeResult {
  bool ok = false;
  int rttMs = 0;
  QString server;     // host:port
  QString error;
  QString errorKind;  // "unreachable", "auth", "other"
  QString relayed;    // relayed transport address, informational
};

struct TestPlan {
  bool relayOnly = false;  // force the relay: both peers may only use TURN candidates
};
struct TestProbeResult {
  bool ok = false;      // data channel opened and echoed
  int rttMs = 0;        // best of a few echoes
  QString pathType;     // "relay", "host", "srflx", "prflx": selected candidate pair type
  int hostCandidates = 0, srflxCandidates = 0, relayCandidates = 0;  // gathered by the first peer
  QString server;       // TURN host:port when relay-only
  QString error;
};

class ConnectionProbes {
 public:
  virtual ~ConnectionProbes() = default;
  virtual void probeHub(const ConnectionTarget& t, std::function<void(const HubProbeResult&)> done) = 0;
  virtual void probeStun(const ConnectionTarget& t, std::function<void(const StunProbeResult&)> done) = 0;
  virtual void probeTurn(const ConnectionTarget& t, std::function<void(const TurnProbeResult&)> done) = 0;
  virtual void probeConnection(const ConnectionTarget& t, const TestPlan& plan, std::function<void(const TestProbeResult&)> done) = 0;
  // Abort what runs; pending callbacks may still come (they are ignored) or never come.
  virtual void cancel() = 0;
};

struct CheckStep {
  CheckStepId id = CheckStepId::Hub;
  CheckStepState state = CheckStepState::Waiting;
  QString result;       // "hub.example.com:8443 · 22 ms", "Waiting", "Skipped"
  QString explanation;  // plain language
  QStringList details;  // technical lines for the text/JSON export only (addresses, server names)
  QString title() const { return stepTitle(id); }
};

struct CheckResult {
  QDateTime finishedAt;  // UTC, invalid while running
  QString hubName, hubAddress, deviceName;
  QList<CheckStep> steps;
  CheckOutcome outcome = CheckOutcome::Running;
  NatType nat = NatType::Unknown;
  QString summary;        // card title
  QString summaryDetail;  // card text

  // Plain text for "Copy result" and the JSON for the diagnostics bundle / problem report. Neither contains credentials.
  QString toPlainText() const;
  QJsonObject toJson() const;
};

// NAT mapping type from the STUN answers of at least two different servers asked from the same socket.
NatType classifyNat(const QList<StunMapping>& mappings);

class ConnectionCheck : public QObject {
  Q_OBJECT
 public:
  explicit ConnectionCheck(std::unique_ptr<ConnectionProbes> probes, QObject* parent = nullptr);
  ~ConnectionCheck() override;

  // Overall limit of a run (default 60 s); the run ends as TimedOut.
  void setTimeoutMs(int ms) { timeoutMs_ = ms; }
  bool running() const { return running_; }
  const CheckResult& result() const { return result_; }

 public slots:
  void start(const framebeam::ConnectionTarget& target);  // restarts when running
  void cancel();

 signals:
  void stepChanged(int index);  // steps()[index] changed
  void progress(int stepNumber, int total);  // "step 3 of 5"
  void finished(const framebeam::CheckResult& result);

 private:
  void begin(int index);
  void runStep(int index);
  void setStep(int index, CheckStepState state, const QString& result, const QString& explanation, const QStringList& details = {});
  void skipRest(int from, const QString& why = {});
  void onHub(const HubProbeResult& r);
  void onStun(const StunProbeResult& r);
  void onTurn(const TurnProbeResult& r);
  void doNat();
  void onTest(const TestProbeResult& r);
  void finish(CheckOutcome outcome);
  void decideOutcome();
  void abort(CheckOutcome why);

  std::unique_ptr<ConnectionProbes> probes_;
  ConnectionTarget target_;
  CheckResult result_;
  QTimer timeout_;
  int timeoutMs_ = 60000;
  quint64 gen_ = 0;  // bumped per run and per cancel: late probe callbacks carry the old value
  bool running_ = false;
  int current_ = -1;
  // Facts collected on the way
  bool stunAnswered_ = false;
  bool turnOk_ = false;
  bool turnOff_ = false;
  bool testRelayOnly_ = false;
  bool testOk_ = false;
  QString turnServer_;
};

}  // namespace framebeam

Q_DECLARE_METATYPE(framebeam::ConnectionTarget)
Q_DECLARE_METATYPE(framebeam::CheckResult)
