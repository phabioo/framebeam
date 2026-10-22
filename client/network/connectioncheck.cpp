#include "connectioncheck.h"

#include <QJsonArray>
#include <QPointer>
#include <QUrl>

namespace framebeam {

namespace {

QString hostPort(const QString& address) {
  const QUrl u(address.contains(QLatin1String("://")) ? address : QStringLiteral("https://") + address);
  const int port = u.port(u.scheme() == QLatin1String("http") ? 80 : 8443);
  return u.host().isEmpty() ? address : QStringLiteral("%1:%2").arg(u.host()).arg(port);
}

// "turn:host:port?transport=udp" -> "host:port" of the first TURN URL.
QString turnHostPort(const QList<TurnServer>& servers) {
  for (const TurnServer& t : servers) {
    for (const QString& u : t.urls) {
      const QString rest = u.section(QLatin1Char(':'), 1).section(QLatin1Char('?'), 0, 0);
      if (!rest.isEmpty()) return rest;
    }
  }
  return {};
}

QString ms(int v) { return QStringLiteral("%1 ms").arg(v); }

}  // namespace

QString stepIdName(CheckStepId id) {
  switch (id) {
    case CheckStepId::Hub: return QStringLiteral("hub");
    case CheckStepId::Stun: return QStringLiteral("stun");
    case CheckStepId::Turn: return QStringLiteral("turn");
    case CheckStepId::Nat: return QStringLiteral("nat");
    case CheckStepId::Test: return QStringLiteral("test");
  }
  return {};
}

QString stepTitle(CheckStepId id) {
  switch (id) {
    case CheckStepId::Hub: return QStringLiteral("Hub reachable");
    case CheckStepId::Stun: return QStringLiteral("STUN");
    case CheckStepId::Turn: return QStringLiteral("TURN relay");
    case CheckStepId::Nat: return QStringLiteral("NAT type");
    case CheckStepId::Test: return QStringLiteral("Test connection");
  }
  return {};
}

QString stepStateName(CheckStepState s) {
  switch (s) {
    case CheckStepState::Waiting: return QStringLiteral("waiting");
    case CheckStepState::Running: return QStringLiteral("running");
    case CheckStepState::Ok: return QStringLiteral("ok");
    case CheckStepState::Warning: return QStringLiteral("warning");
    case CheckStepState::Failed: return QStringLiteral("failed");
    case CheckStepState::Skipped: return QStringLiteral("skipped");
  }
  return {};
}

QString outcomeName(CheckOutcome o) {
  switch (o) {
    case CheckOutcome::Running: return QStringLiteral("running");
    case CheckOutcome::Direct: return QStringLiteral("direct");
    case CheckOutcome::Relayed: return QStringLiteral("relayed");
    case CheckOutcome::Blocked: return QStringLiteral("blocked");
    case CheckOutcome::HubUnreachable: return QStringLiteral("hub_unreachable");
    case CheckOutcome::NotConnected: return QStringLiteral("not_connected");
    case CheckOutcome::Canceled: return QStringLiteral("canceled");
    case CheckOutcome::TimedOut: return QStringLiteral("timed_out");
  }
  return {};
}

QString natTypeName(NatType t) {
  switch (t) {
    case NatType::Unknown: return QStringLiteral("unknown");
    case NatType::None: return QStringLiteral("none");
    case NatType::EndpointIndependent: return QStringLiteral("endpoint_independent");
    case NatType::Symmetric: return QStringLiteral("symmetric");
  }
  return {};
}

NatType classifyNat(const QList<StunMapping>& mappings) {
  QStringList servers, mapped;
  bool anyNat = false;
  for (const StunMapping& m : mappings) {
    if (!m.answered() || servers.contains(m.server)) continue;
    servers << m.server;
    mapped << m.mapped;
    anyNat = anyNat || m.behindNat;
  }
  if (servers.isEmpty()) return NatType::Unknown;
  if (!anyNat) return NatType::None;
  if (servers.size() < 2) return NatType::Unknown;
  mapped.removeDuplicates();
  return mapped.size() == 1 ? NatType::EndpointIndependent : NatType::Symmetric;
}

// ---------------------------------------------------------------------------------------------------------------- export

QString CheckResult::toPlainText() const {
  QStringList out;
  out << QStringLiteral("FrameBeam connection check");
  out << QStringLiteral("%1 to %2 (%3)").arg(deviceName.isEmpty() ? QStringLiteral("This device") : deviceName, hubName, hubAddress);
  if (finishedAt.isValid()) out << finishedAt.toUTC().toString(Qt::ISODate);
  out << QString();
  int n = 1;
  for (const CheckStep& s : steps) {
    out << QStringLiteral("%1. %2: %3 (%4)").arg(n++).arg(s.title(), s.result, stepStateName(s.state));
    if (!s.explanation.isEmpty()) out << QStringLiteral("   ") + s.explanation;
    for (const QString& d : s.details) out << QStringLiteral("   - ") + d;
  }
  out << QString();
  if (!summary.isEmpty()) out << summary;
  if (!summaryDetail.isEmpty()) out << summaryDetail;
  return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QJsonObject CheckResult::toJson() const {
  QJsonArray arr;
  for (const CheckStep& s : steps) {
    arr.append(QJsonObject{{QStringLiteral("id"), stepIdName(s.id)},
                           {QStringLiteral("title"), s.title()},
                           {QStringLiteral("state"), stepStateName(s.state)},
                           {QStringLiteral("result"), s.result},
                           {QStringLiteral("explanation"), s.explanation},
                           {QStringLiteral("details"), QJsonArray::fromStringList(s.details)}});
  }
  return QJsonObject{{QStringLiteral("format"), 1},
                     {QStringLiteral("kind"), QStringLiteral("connection_check")},
                     {QStringLiteral("generated_at"), finishedAt.isValid() ? finishedAt.toUTC().toString(Qt::ISODate) : QString()},
                     {QStringLiteral("device"), deviceName},
                     {QStringLiteral("hub"), QJsonObject{{QStringLiteral("name"), hubName}, {QStringLiteral("address"), hubAddress}}},
                     {QStringLiteral("outcome"), outcomeName(outcome)},
                     {QStringLiteral("nat_type"), natTypeName(nat)},
                     {QStringLiteral("summary"), summary},
                     {QStringLiteral("summary_detail"), summaryDetail},
                     {QStringLiteral("steps"), arr}};
}

// ------------------------------------------------------------------------------------------------------------- sequencing

ConnectionCheck::ConnectionCheck(std::unique_ptr<ConnectionProbes> probes, QObject* parent)
    : QObject(parent), probes_(std::move(probes)) {
  qRegisterMetaType<framebeam::ConnectionTarget>();
  qRegisterMetaType<framebeam::CheckResult>();
  timeout_.setSingleShot(true);
  connect(&timeout_, &QTimer::timeout, this, [this]() { abort(CheckOutcome::TimedOut); });
}

ConnectionCheck::~ConnectionCheck() {
  ++gen_;
  if (probes_) probes_->cancel();
}

void ConnectionCheck::start(const ConnectionTarget& target) {
  if (running_) {
    ++gen_;
    probes_->cancel();
  }
  ++gen_;
  target_ = target;
  result_ = CheckResult{};
  result_.hubName = target.hubName;
  result_.hubAddress = target.hubAddress;
  result_.deviceName = target.deviceName;
  for (int i = 0; i < kCheckStepCount; ++i) {
    CheckStep s;
    s.id = static_cast<CheckStepId>(i);
    s.result = QStringLiteral("Waiting");
    result_.steps.append(s);
  }
  stunAnswered_ = turnOk_ = turnOff_ = testRelayOnly_ = testOk_ = false;
  turnServer_.clear();
  running_ = true;
  timeout_.start(timeoutMs_);
  for (int i = 0; i < kCheckStepCount; ++i) emit stepChanged(i);
  begin(0);
}

void ConnectionCheck::cancel() {
  if (running_) abort(CheckOutcome::Canceled);
}

void ConnectionCheck::setStep(int index, CheckStepState state, const QString& result, const QString& explanation, const QStringList& details) {
  CheckStep& s = result_.steps[index];
  s.state = state;
  s.result = result;
  s.explanation = explanation;
  s.details = details;
  emit stepChanged(index);
}

void ConnectionCheck::skipRest(int from, const QString& why) {
  for (int i = from; i < kCheckStepCount; ++i) setStep(i, CheckStepState::Skipped, why.isEmpty() ? QStringLiteral("Skipped") : why, {});
}

void ConnectionCheck::begin(int index) {
  current_ = index;
  emit progress(index + 1, kCheckStepCount);
  runStep(index);
}

void ConnectionCheck::runStep(int index) {
  const quint64 gen = gen_;
  QPointer<ConnectionCheck> self(this);
  // A late callback of an older run or after cancel() is dropped.
  const auto live = [self, gen]() { return !self.isNull() && self->gen_ == gen && self->running_; };
  const QString hub = hostPort(target_.hubAddress);
  switch (static_cast<CheckStepId>(index)) {
    case CheckStepId::Hub:
      setStep(index, CheckStepState::Running, QStringLiteral("Testing %1…").arg(hub), {});
      probes_->probeHub(target_, [self, live](const HubProbeResult& r) { if (live()) self->onHub(r); });
      break;
    case CheckStepId::Stun:
      setStep(index, CheckStepState::Running, QStringLiteral("Asking the STUN servers…"), {});
      probes_->probeStun(target_, [self, live](const StunProbeResult& r) { if (live()) self->onStun(r); });
      break;
    case CheckStepId::Turn: {
      turnServer_ = turnHostPort(target_.turnServers);
      if (turnServer_.isEmpty()) {
        turnOff_ = true;
        setStep(index, CheckStepState::Skipped, QStringLiteral("Not turned on"),
                QStringLiteral("%1 has no relay (TURN). Sessions across networks then work only when a direct connection is possible. "
                               "An admin can turn the relay on in the Hub's Network settings.").arg(target_.hubName));
        begin(index + 1);
        break;
      }
      setStep(index, CheckStepState::Running, QStringLiteral("Testing %1…").arg(turnServer_), {});
      probes_->probeTurn(target_, [self, live](const TurnProbeResult& r) { if (live()) self->onTurn(r); });
      break;
    }
    case CheckStepId::Nat:
      doNat();
      break;
    case CheckStepId::Test: {
      const bool forced = target_.forceRelay;
      testRelayOnly_ = turnOk_ && (forced || !stunAnswered_ || result_.nat == NatType::Symmetric);
      setStep(index, CheckStepState::Running, testRelayOnly_ ? QStringLiteral("Connecting two test peers through the relay…")
                                                              : QStringLiteral("Connecting two test peers…"), {});
      TestPlan plan;
      plan.relayOnly = testRelayOnly_;
      probes_->probeConnection(target_, plan, [self, live](const TestProbeResult& r) { if (live()) self->onTest(r); });
      break;
    }
  }
}

void ConnectionCheck::onHub(const HubProbeResult& r) {
  const QString hub = hostPort(target_.hubAddress);
  if (r.ok) {
    setStep(0, CheckStepState::Ok, QStringLiteral("%1 · %2").arg(hub, ms(r.latencyMs)), QStringLiteral("The Player can talk to %1.").arg(target_.hubName));
    if (!target_.iceKnown) {
      skipRest(1, QStringLiteral("Skipped"));
      finish(CheckOutcome::NotConnected);
      return;
    }
    begin(1);
    return;
  }
  const int port = hub.section(QLatin1Char(':'), -1).toInt();
  QString res, expl;
  if (r.errorKind == QLatin1String("timeout")) {
    res = r.latencyMs >= 1000 ? QStringLiteral("No answer from %1 after %2 s").arg(hub).arg(qRound(r.latencyMs / 1000.0)) : QStringLiteral("No answer from %1").arg(hub);
    expl = QStringLiteral("Either the Hub is off, the address is wrong, or a firewall blocks port %1.").arg(port);
  } else if (r.errorKind == QLatin1String("refused")) {
    res = QStringLiteral("%1 refused the connection").arg(hub);
    expl = QStringLiteral("The computer answers, but nothing listens on port %1. Check that the Hub is running and the port is right.").arg(port);
  } else if (r.errorKind == QLatin1String("dns")) {
    res = QStringLiteral("The name %1 cannot be found").arg(hub.section(QLatin1Char(':'), 0, -2));
    expl = QStringLiteral("Check the address for typos, and that this device is online.");
  } else if (r.errorKind == QLatin1String("certificate")) {
    res = QStringLiteral("The certificate of %1 does not match").arg(hub);
    expl = QStringLiteral("This is not the Hub this Player has trusted, or its certificate changed. Nothing was sent. Check the Hub under Settings → Hubs.");
  } else if (r.errorKind == QLatin1String("http")) {
    res = QStringLiteral("%1 does not answer like a Hub").arg(hub);
    expl = QStringLiteral("Something answers on this address, but it is not a FrameBeam Hub (or an incompatible version). Check the address.");
  } else {
    res = QStringLiteral("%1 cannot be reached").arg(hub);
    expl = r.error.isEmpty() ? QStringLiteral("The connection failed.") : r.error;
  }
  setStep(0, CheckStepState::Failed, res, expl, r.error.isEmpty() ? QStringList{} : QStringList{r.error});
  skipRest(1);
  result_.nat = NatType::Unknown;
  finish(CheckOutcome::HubUnreachable);
}

void ConnectionCheck::onStun(const StunProbeResult& r) {
  QStringList details;
  int answered = 0;
  for (const StunMapping& m : r.mappings) {
    if (m.answered()) {
      ++answered;
      details << QStringLiteral("%1 saw %2 (%3)").arg(m.server, m.mapped, ms(m.rttMs));
    } else {
      details << QStringLiteral("%1: no answer").arg(m.server);
    }
  }
  result_.nat = classifyNat(r.mappings);  // used by steps 3 to 5; step 4 only reports it
  if (r.mappings.isEmpty()) {
    setStep(1, CheckStepState::Skipped, QStringLiteral("No STUN server"),
            QStringLiteral("%1 hands out no STUN server (it is set up for a local network). Players then find each other on the same network, or through the relay.").arg(target_.hubName));
    begin(2);
    return;
  }
  if (answered == 0) {
    setStep(1, CheckStepState::Failed, QStringLiteral("No answer from the STUN servers"),
            QStringLiteral("Other Players cannot learn your public address, so direct connections are unlikely. UDP may be blocked by a firewall."), details);
  } else {
    stunAnswered_ = true;
    setStep(1, CheckStepState::Ok, QStringLiteral("Public address found"), QStringLiteral("Other Players can find you."), details);
  }
  begin(2);
}

void ConnectionCheck::onTurn(const TurnProbeResult& r) {
  const QString server = r.server.isEmpty() ? turnServer_ : r.server;
  if (r.ok) {
    turnOk_ = true;
    QStringList d;
    if (!r.relayed.isEmpty()) d << QStringLiteral("relayed address %1").arg(r.relayed);
    setStep(2, CheckStepState::Ok, QStringLiteral("Works · %1").arg(ms(r.rttMs)), QStringLiteral("Used only when a direct connection fails."), d);
  } else if (r.errorKind == QLatin1String("auth")) {
    setStep(2, CheckStepState::Failed, QStringLiteral("The relay refused the Player's credentials"),
            QStringLiteral("The relay credentials are short-lived and come from %1. Reconnect to the Hub and run the check again.").arg(target_.hubName),
            r.error.isEmpty() ? QStringList{} : QStringList{r.error});
  } else {
    setStep(2, CheckStepState::Failed, QStringLiteral("No answer from %1").arg(server),
            QStringLiteral("A firewall may block the relay port. Without the relay, Sessions work only when a direct connection is possible."),
            r.error.isEmpty() ? QStringList{} : QStringList{r.error});
  }
  begin(3);
}

void ConnectionCheck::doNat() {
  const NatType nat = result_.nat;
  switch (nat) {
    case NatType::None:
      setStep(3, CheckStepState::Ok, QStringLiteral("No NAT"), QStringLiteral("This device has a public address, so direct connections work."));
      break;
    case NatType::EndpointIndependent:
      setStep(3, CheckStepState::Ok, QStringLiteral("Open · endpoint-independent"), QStringLiteral("Allows direct connections with most networks."));
      break;
    case NatType::Symmetric:
      setStep(3, CheckStepState::Warning, QStringLiteral("Symmetric NAT"),
              QStringLiteral("Your network changes the port for every connection, so direct connections fail."));
      break;
    case NatType::Unknown:
      if (stunAnswered_) {
        setStep(3, CheckStepState::Warning, QStringLiteral("Not determined"),
                QStringLiteral("Only one STUN server answered, so its answer cannot be compared with another."));
      } else {
        setStep(3, CheckStepState::Skipped, QStringLiteral("Skipped"), QStringLiteral("Needs an answer from a STUN server."));
      }
      break;
  }
  begin(4);
}

void ConnectionCheck::onTest(const TestProbeResult& r) {
  QStringList details;
  details << QStringLiteral("candidates: host=%1 srflx=%2 relay=%3").arg(r.hostCandidates).arg(r.srflxCandidates).arg(r.relayCandidates);
  if (!r.pathType.isEmpty()) details << QStringLiteral("selected path: %1").arg(r.pathType);
  if (!r.ok) {
    setStep(4, CheckStepState::Failed, QStringLiteral("The test connection failed"),
            testRelayOnly_ ? QStringLiteral("Two test peers could not exchange data through the relay%1.").arg(r.error.isEmpty() ? QString() : QStringLiteral(" (") + r.error + QLatin1Char(')'))
                           : QStringLiteral("Two test peers in this Player could not connect%1.").arg(r.error.isEmpty() ? QString() : QStringLiteral(" (") + r.error + QLatin1Char(')')),
            details);
  } else if (testRelayOnly_) {
    testOk_ = true;
    const QString via = r.server.isEmpty() ? turnServer_ : r.server;
    setStep(4, CheckStepState::Warning, QStringLiteral("Relayed through %1 · %2").arg(via, ms(r.rttMs)),
            target_.forceRelay ? QStringLiteral("The relay is forced on this Player (FRAMEBEAM_FORCE_RELAY). Data flowed between two test peers through the relay.")
                               : QStringLiteral("Data flowed between two test peers in this Player through the relay of %1.").arg(target_.hubName),
            details);
  } else if (r.srflxCandidates > 0) {
    testOk_ = true;
    setStep(4, CheckStepState::Ok, QStringLiteral("WebRTC works · public address found"),
            QStringLiteral("Two test peers in this Player connected and found a public address through the Hub's servers. Whether a direct path to another Player works also depends on that Player's network."),
            details);
  } else {
    setStep(4, CheckStepState::Warning, QStringLiteral("No public address found"),
            QStringLiteral("The test peers connected, but WebRTC found no public address through the STUN servers, so direct connections to other networks are unlikely."), details);
  }
  decideOutcome();
}

void ConnectionCheck::decideOutcome() {
  const QString hub = target_.hubName;
  const bool noRoute = target_.stunUrls.isEmpty() && target_.turnServers.isEmpty();
  const bool direct = stunAnswered_ && result_.nat != NatType::Symmetric && !target_.forceRelay && testOk_ && !testRelayOnly_;
  if (direct) {
    result_.summary = QStringLiteral("Your network allows direct connections");
    result_.summaryDetail = turnOk_ ? QStringLiteral("Sessions go straight between Players. The relay is ready as a fallback.")
                          : turnOff_ ? QStringLiteral("Sessions go straight between Players. %1 has no relay, so a Session fails if a direct connection is not possible.").arg(hub)
                                     : QStringLiteral("Sessions go straight between Players. The relay did not work (see step 3), so a Session fails if a direct connection is not possible.");
    finish(CheckOutcome::Direct);
  } else if (turnOk_ && (!testRelayOnly_ || testOk_)) {
    result_.steps[2].explanation = QStringLiteral("Sessions will use the relay on this network.");
    emit stepChanged(2);
    result_.summary = QStringLiteral("Only relayed connections are possible here");
    result_.summaryDetail = target_.forceRelay
        ? QStringLiteral("The relay is forced on this Player (FRAMEBEAM_FORCE_RELAY), so every Session uses it. This works but adds delay.")
        : QStringLiteral("This works but adds delay. Common on office and mobile networks. Turning on UPnP on your router, or using another network, usually allows direct connections.");
    finish(CheckOutcome::Relayed);
  } else {
    result_.summary = QStringLiteral("Connections to other networks are not possible from here");
    result_.summaryDetail = noRoute
        ? QStringLiteral("%1 hands out no STUN server and has no relay, so Sessions work between devices on the same network only. An admin can change this in the Hub's Network settings.").arg(hub)
        : QStringLiteral("Your network blocks direct connections and the relay does not work or is not turned on at %1. Sessions work between devices on the same network. Try another network, or ask the Hub's admin to check the relay.").arg(hub);
    finish(CheckOutcome::Blocked);
  }
}

void ConnectionCheck::abort(CheckOutcome why) {
  ++gen_;
  if (probes_) probes_->cancel();
  const bool timedOut = why == CheckOutcome::TimedOut;
  for (int i = 0; i < kCheckStepCount; ++i) {
    CheckStep& s = result_.steps[i];
    if (s.state == CheckStepState::Running) {
      setStep(i, timedOut ? CheckStepState::Failed : CheckStepState::Skipped, timedOut ? QStringLiteral("Timed out") : QStringLiteral("Canceled"),
              timedOut ? QStringLiteral("This step did not finish in time.") : QString());
    } else if (s.state == CheckStepState::Waiting) {
      setStep(i, CheckStepState::Skipped, QStringLiteral("Skipped"), {});
    }
  }
  result_.summary = timedOut ? QStringLiteral("The check did not finish") : QStringLiteral("The check was canceled");
  result_.summaryDetail = timedOut ? QStringLiteral("A step took too long. Run the check again; if it keeps timing out, a firewall may be blocking the connection.") : QString();
  finish(why);
}

void ConnectionCheck::finish(CheckOutcome outcome) {
  if (outcome == CheckOutcome::HubUnreachable) {
    result_.summary = QStringLiteral("%1 cannot be reached").arg(target_.hubName);
    result_.summaryDetail = QStringLiteral("Check that the Hub is running and that %1 is the right address. Other steps need the Hub and were skipped.").arg(hostPort(target_.hubAddress));
  }
  if (outcome == CheckOutcome::NotConnected) {
    result_.summary = QStringLiteral("Connect to %1 to run the full check").arg(target_.hubName);
    result_.summaryDetail = QStringLiteral("%1 answers, but this Player is not connected to it right now, so it does not know the Hub's STUN and relay servers. "
                                           "Connect to the Hub (Settings → Hubs) and run the check again.").arg(target_.hubName);
  }
  running_ = false;
  timeout_.stop();
  result_.outcome = outcome;
  result_.finishedAt = QDateTime::currentDateTimeUtc();
  emit finished(result_);
}

}  // namespace framebeam
