#include "onboardingcontroller.h"

#include <QDateTime>
#include <QUrl>
#include <algorithm>

#include "securitycode.h"

namespace uzume::ui {

using State = HubConnection::State;

namespace {

bool hasSignedInAccount(const HubProfile& p) {
  return std::any_of(p.accounts.begin(), p.accounts.end(), [](const HubAccount& a) { return !a.credentialRef.isEmpty(); });
}

QString withScheme(const QString& address) {
  return address.contains(QLatin1String("://")) ? address : QStringLiteral("https://") + address;
}

}  // namespace

OnboardingController::OnboardingController(const Deps& deps, QObject* parent) : QObject(parent), d_(deps) {
  connect(d_.conn, &HubConnection::stateChanged, this, &OnboardingController::onState);
  connect(d_.conn, &HubConnection::errorOccurred, this, &OnboardingController::onError);
}

OnboardingController::~OnboardingController() = default;

// ---------------------------------------------------------------- Properties

bool OnboardingController::firstStart() const {
  return d_.profiles->profiles().isEmpty() && d_.settings->libraryFolders().isEmpty() && !d_.settings->playOnThisPcChosen();
}

void OnboardingController::setStartScreenEnabled(bool on) {
  if (startScreenEnabled_ == on) {
    return;
  }
  startScreenEnabled_ = on;
  emit changed();
}

bool OnboardingController::takesScreen() const {
  switch (stage_) {
    case Stage::Idle:
      return startScreenEnabled_ && firstStart() && d_.conn->state() != State::Connected;
    case Stage::Address:
    case Stage::LinkChoice:
    case Stage::LinkMember:
    case Stage::Play:
      return true;
    case Stage::Active:
      return d_.conn->state() != State::Connected;
  }
  return false;
}

QString OnboardingController::phase() const {
  switch (stage_) {
    case Stage::Idle:
      return QStringLiteral("idle");
    case Stage::Address:
      return mismatch_ ? QStringLiteral("security_mismatch") : QStringLiteral("address");
    case Stage::LinkChoice:
      return QStringLiteral("link_choice");
    case Stage::LinkMember:
      return QStringLiteral("link_member");
    case Stage::Play:
      return QStringLiteral("play");
    case Stage::Active:
      break;
  }
  if (mismatch_) {
    return QStringLiteral("security_mismatch");
  }
  switch (d_.conn->state()) {
    case State::Disconnected: return QStringLiteral("address");
    case State::Identifying:
    case State::Authenticating: return QStringLiteral("checking");
    case State::NeedsTrustConfirmation: return QStringLiteral("security");
    case State::CertificateChanged: return QStringLiteral("certificate_changed");
    case State::Incompatible:
      return d_.conn->incompatibleReason() == HubConnection::IncompatibleReason::PlayerTooOld ? QStringLiteral("app_too_old")
                                                                                              : QStringLiteral("server_too_old");
    case State::Unreachable: return QStringLiteral("offline");
    case State::NeedsPairing:
    case State::Denied:
    case State::Expired:
      break;
    case State::AwaitingApproval: return QStringLiteral("approval_wait");
    case State::UserDisabled: return QStringLiteral("disabled");
    case State::Connected: return QStringLiteral("connected");
  }
  if (d_.conn->state() == State::Denied) {
    return QStringLiteral("approval_denied");
  }
  if (d_.conn->state() == State::Expired) {
    return QStringLiteral("approval_expired");
  }
  if (flow_ == QLatin1String("setup")) {
    return d_.conn->setupRequired() ? QStringLiteral("setup_account") : QStringLiteral("setup_exists");
  }
  if (flow_ == QLatin1String("link") && !linkContinued_ && d_.conn->trustedByLink()) {
    return QStringLiteral("link_ready");
  }
  return view_ == View::Invite ? QStringLiteral("invite") : QStringLiteral("sign_in");
}

QString OnboardingController::displayAddress(const QString& address) {
  QString a = address;
  if (const qsizetype sep = a.indexOf(QLatin1String("://")); sep >= 0) {
    a = a.mid(sep + 3);
  }
  while (a.endsWith(QLatin1Char('/'))) {
    a.chop(1);
  }
  return a;
}

QString OnboardingController::serverName() const {
  if (!d_.conn->hubInfo().name.isEmpty()) {
    return d_.conn->hubInfo().name;
  }
  if (const auto p = d_.conn->profile(); p && !p->name.isEmpty()) {
    return p->name;
  }
  return serverAddress();
}

QString OnboardingController::serverAddress() const {
  return d_.conn->address().isEmpty() ? typedAddress_ : displayAddress(d_.conn->address());
}

QString OnboardingController::serverVersion() const { return d_.conn->hubInfo().hubVersion; }
QString OnboardingController::securityCode() const { return d_.conn->securityCode(); }
bool OnboardingController::setupRequired() const { return d_.conn->setupRequired(); }

QVariantList OnboardingController::recentServers() const {
  QList<HubProfile> list = d_.profiles->profiles();
  std::sort(list.begin(), list.end(), [](const HubProfile& a, const HubProfile& b) {
    if (a.lastConnected.isValid() != b.lastConnected.isValid()) return a.lastConnected.isValid();
    return a.lastConnected > b.lastConnected;
  });
  QVariantList out;
  for (const HubProfile& p : std::as_const(list)) {
    out.append(QVariantMap{{QStringLiteral("hubId"), p.hubId},
                           {QStringLiteral("name"), p.name.isEmpty() ? displayAddress(p.address) : p.name},
                           {QStringLiteral("address"), displayAddress(p.address)},
                           {QStringLiteral("lastUsed"), p.lastConnected.isValid() ? p.lastConnected.toUTC().toString(Qt::ISODate) : QString()},
                           {QStringLiteral("selected"), p.hubId == selectedHubId_}});
  }
  return out;
}

QVariantMap OnboardingController::linkChoice() const {
  if (stage_ != Stage::LinkChoice) {
    return {};
  }
  const auto linked = d_.profiles->profileByAddress(withScheme(linkAddress_));
  return QVariantMap{{QStringLiteral("currentName"), d_.conn->hubInfo().name},
                     {QStringLiteral("currentUser"), d_.conn->userDisplayName()},
                     {QStringLiteral("linkedName"), linked ? linked->name : QString()},
                     {QStringLiteral("linkedAddress"), linkAddress_},
                     {QStringLiteral("codeFromLink"), true}};
}

QVariantMap OnboardingController::linkInfo() const {
  if (linkAddress_.isEmpty() || flow_ != QLatin1String("link")) {
    return {};
  }
  const auto known = d_.profiles->profileByAddress(withScheme(linkAddress_));
  return QVariantMap{{QStringLiteral("name"), stage_ == Stage::Active ? serverName() : (known ? known->name : QString())},
                     {QStringLiteral("address"), linkAddress_},
                     {QStringLiteral("securityMatches"), d_.conn->trustedByLink()},
                     {QStringLiteral("member"), stage_ == Stage::LinkMember}};
}

// ---------------------------------------------------------------- Errors

QString OnboardingController::durationText(int seconds) {
  return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString OnboardingController::messageFor(const QString& code, const QString& serverMessage, int retryAfter) const {
  const QString server = serverName();
  const QString address = serverAddress();
  if (code == QLatin1String("invalid_credentials")) return tr("Wrong name or password.");
  if (code == QLatin1String("rate_limited")) {
    return retryAfter > 0 ? tr("Too many wrong tries. Try again in %1.").arg(durationText(retryAfter))
                          : tr("Too many tries. Please wait a moment and try again.");
  }
  if (code == QLatin1String("user_disabled")) return tr("This account has been turned off on %1. Your saves are kept.").arg(server);
  if (code == QLatin1String("conflict")) return tr("This PC could not be signed in. Please try again.");
  if (code == QLatin1String("unreachable")) return tr("No answer from %1 after 10 seconds.").arg(address);
  if (code == QLatin1String("hub_too_old")) {
    const QString version = serverVersion();
    return (version.isEmpty() ? tr("%1 is too old for this version of Uzume.").arg(server)
                              : tr("%1 runs %2 and is too old for this version of Uzume.").arg(server, version)) +
           QLatin1Char(' ') + tr("Update the server in Settings > Server > Updates or with `uzume-server update`.");
  }
  if (code == QLatin1String("player_too_old")) return tr("This version of Uzume is too old for %1. Update Uzume to continue.").arg(server);
  if (code == QLatin1String("invalid_input")) return tr("Enter your name and password.");
  if (code == QLatin1String("bad_request")) return tr("Check the name and the password. Passwords need at least 12 characters.");
  if (code == QLatin1String("invite_invalid")) return tr("This invite has expired, or all its uses are gone.");
  if (code == QLatin1String("display_name_taken")) {
    return tr("Someone on %1 is already called that. Add a letter or pick another name.").arg(server);
  }
  if (code == QLatin1String("invalid_setup_code")) return tr("That code doesn't match. Codes are 6 letters and numbers.");
  if (code == QLatin1String("setup_code_expired")) {
    return tr("That setup code has expired. Run `uzume-server setup-code` on the device for a new one.");
  }
  if (code == QLatin1String("already_set_up")) return tr("%1 is already set up.").arg(server);
  if (code == QLatin1String("security_code_mismatch")) {
    return tr("Stop: the codes don't match. Someone may be in between. Nothing was sent to the server.");
  }
  if (code == QLatin1String("certificate_changed")) return tr("The security certificate of %1 has changed.").arg(server);
  if (code == QLatin1String("invalid_address")) return tr("Enter an address like server.example.com:8443, or an invite code.");
  if (code == QLatin1String("invalid_invite_code")) return tr("That doesn't look like an invite code. Codes look like UZ-8XK2-M4QD.");
  if (code == QLatin1String("broken_link")) return tr("This link doesn't work. Paste the invite code instead (UZ-XXXX-XXXX).");
  if (code == QLatin1String("need_address")) return tr("Which server is this invite for? Enter its address.");
  if (code == QLatin1String("insecure_http")) return tr("Plain HTTP is only allowed for this PC. Use an https address.");
  if (code == QLatin1String("invalid_hub_info")) return tr("The address does not answer like an Uzume server.");
  if (code == QLatin1String("credential_store_failed")) {
    return tr("The sign-in could not be saved to the credential store of this PC.");
  }
  return serverMessage.isEmpty() ? tr("Something went wrong (%1).").arg(code) : serverMessage;
}

void OnboardingController::setError(const QString& code, const QString& message, int retryAfter) {
  errorCode_ = code;
  retryAfter_ = retryAfter;
  errorMessage_ = messageFor(code, message, retryAfter);
  emit changed();
}

void OnboardingController::clearError() {
  errorCode_.clear();
  errorMessage_.clear();
  retryAfter_ = 0;
}

void OnboardingController::onError(const QString& code, const QString& message) {
  if (stage_ == Stage::Idle || stage_ == Stage::Play || code.isEmpty()) {
    return;
  }
  busy_ = false;
  if (flow_ == QLatin1String("setup") && (code == QLatin1String("invalid_setup_code") || code == QLatin1String("setup_code_expired"))) {
    // The code is wrong or too old: back to the field next to the address, the connection and the pinned certificate are kept.
    setupCode_.clear();
    stage_ = Stage::Address;
    leaveConnection();
  }
  setError(code, message, d_.conn->errorRetryAfter());
}

// ---------------------------------------------------------------- Connection state

void OnboardingController::onState(State s) {
  if (stage_ == Stage::Idle || stage_ == Stage::Play) {
    emit changed();  // firstStart / takesScreen follow the connection
    return;
  }
  if (s == State::Connected) {
    complete();
    return;
  }
  if (s != State::Authenticating && s != State::NeedsPairing) {
    busy_ = false;
  }
  if (s == State::Disconnected && stage_ == Stage::Active) {
    if (d_.conn->errorCode() == QLatin1String("security_code_mismatch")) {
      mismatch_ = true;  // the hard stop of an invite link; onError adds the message
    } else {
      stage_ = Stage::Address;
    }
  }
  emit changed();
}

void OnboardingController::complete() {
  stage_ = Stage::Idle;
  flow_.clear();
  busy_ = false;
  mismatch_ = false;
  clearError();
  setupCode_.clear();
  inviteCode_.clear();
  securityChecked_ = false;
  linkContinued_ = false;
  linkAddress_.clear();
  linkFingerprint_.clear();
  selectedHubId_.clear();
  view_ = View::SignIn;
  emit changed();
}

void OnboardingController::refresh() { emit changed(); }

// Drops a connection attempt that did not end on a server (nothing open is touched), and the server entry that only this
// attempt created.
void OnboardingController::leaveConnection() {
  if (d_.conn->state() == State::Connected || d_.conn->state() == State::Disconnected) {
    return;
  }
  const QString hubId = d_.conn->hubId();
  d_.conn->disconnectFromHub();
  if (!hubId.isEmpty() && !profileExistedBefore_) {
    if (const auto p = d_.profiles->profile(hubId); p && p->accounts.isEmpty()) {
      d_.conn->removeProfile(hubId);  // saved when the certificate was accepted, never joined
    }
  }
}

// ---------------------------------------------------------------- Start

void OnboardingController::cancel() {
  const bool active = stage_ == Stage::Active;
  stage_ = Stage::Idle;
  flow_.clear();
  busy_ = false;
  mismatch_ = false;
  clearError();
  setupCode_.clear();
  inviteCode_.clear();
  securityChecked_ = false;
  linkContinued_ = false;
  linkAddress_.clear();
  linkFingerprint_.clear();
  view_ = View::SignIn;
  probe_.reset();
  if (active) {
    leaveConnection();
  }
  emit changed();
}

void OnboardingController::back() {
  clearError();
  switch (stage_) {
    case Stage::Idle:
      return;
    case Stage::Address:
      cancel();
      return;
    case Stage::LinkChoice:
    case Stage::LinkMember:
    case Stage::Play:
      cancel();
      return;
    case Stage::Active:
      break;
  }
  mismatch_ = false;
  if (d_.conn->state() == State::NeedsTrustConfirmation) {
    stage_ = Stage::Address;
    d_.conn->rejectTrust();
  } else if ((d_.conn->state() == State::NeedsPairing || d_.conn->state() == State::Denied || d_.conn->state() == State::Expired) &&
             view_ == View::Invite && flow_ != QLatin1String("link") && flow_ != QLatin1String("setup")) {
    view_ = View::SignIn;
  } else {
    stage_ = Stage::Address;
    leaveConnection();
  }
  emit changed();
}

void OnboardingController::beginPlay() {
  if (stage_ == Stage::Active) {
    leaveConnection();
  }
  flow_ = QStringLiteral("play");
  stage_ = Stage::Play;
  mismatch_ = false;
  busy_ = false;
  clearError();
  emit changed();
}

void OnboardingController::choosePlayOnThisPc() {
  d_.settings->setPlayOnThisPcChosen(true);
  if (d_.startLocalHub) {
    d_.startLocalHub();
  }
  emit changed();
}

void OnboardingController::addGameFolder(const QString& pathOrUrl) {
  if (d_.addLibraryFolder) {
    d_.addLibraryFolder(pathOrUrl);
  }
  emit changed();
}

void OnboardingController::finishPlayOnThisPc() {
  if (stage_ == Stage::Play) {
    stage_ = Stage::Idle;
    flow_.clear();
  }
  choosePlayOnThisPc();
}

// ---------------------------------------------------------------- Join

void OnboardingController::beginJoin() {
  if (stage_ == Stage::Active) {
    leaveConnection();
  }
  flow_ = QStringLiteral("join");
  stage_ = Stage::Address;
  view_ = View::SignIn;
  mismatch_ = false;
  busy_ = false;
  inviteCode_.clear();
  setupCode_.clear();
  linkContinued_ = false;
  clearError();
  emit changed();
}

void OnboardingController::selectRecentServer(const QString& hubId) {
  selectedHubId_ = d_.profiles->profile(hubId) ? hubId : QString();
  emit changed();
}

void OnboardingController::connectAddress(const QString& address) {
  typedAddress_ = displayAddress(address);
  const auto known = d_.profiles->profileByAddress(withScheme(address));
  profileExistedBefore_ = known.has_value();
  stage_ = Stage::Active;
  securityChecked_ = false;
  linkContinued_ = false;
  if (known && !known->accounts.isEmpty() && view_ == View::Invite) {
    d_.conn->beginAddAccount(known->hubId);  // another person on a known server: nobody is signed in by this
  } else {
    d_.conn->connectToAddress(address, d_.allowHttp);
  }
  emit changed();
}

void OnboardingController::connectKnownServer(const QString& hubId) {
  const auto p = d_.profiles->profile(hubId);
  if (!p) {
    stage_ = Stage::Address;
    setError(QStringLiteral("invalid_address"));
    return;
  }
  typedAddress_ = displayAddress(p->address);
  profileExistedBefore_ = true;
  stage_ = Stage::Active;
  securityChecked_ = false;
  linkContinued_ = false;
  if (!p->accounts.isEmpty() && view_ == View::Invite) {
    d_.conn->beginAddAccount(hubId);
  } else {
    d_.conn->connectToProfile(hubId);
  }
  emit changed();
}

void OnboardingController::submitJoinInput(const QString& text) {
  if (flow_.isEmpty() || flow_ == QLatin1String("setup")) {
    flow_ = QStringLiteral("join");
  }
  if (stage_ == Stage::Active) {
    leaveConnection();
  }
  clearError();
  mismatch_ = false;
  busy_ = false;
  if (text.trimmed().isEmpty() && !selectedHubId_.isEmpty()) {
    connectKnownServer(selectedHubId_);
    return;
  }
  const JoinInput in = classifyJoinInput(text);
  switch (in.kind) {
    case JoinInput::Kind::Empty:
      stage_ = Stage::Address;
      setError(QStringLiteral("invalid_address"));
      return;
    case JoinInput::Kind::Invalid:
    case JoinInput::Kind::BrokenLink:
      stage_ = Stage::Address;
      setError(in.error);
      return;
    case JoinInput::Kind::InviteCode:
      inviteCode_ = in.inviteCode;
      view_ = View::Invite;
      if (!selectedHubId_.isEmpty()) {
        connectKnownServer(selectedHubId_);
      } else {
        stage_ = Stage::Address;  // the code names no server: ask for the address, the code is kept
        setError(QStringLiteral("need_address"));
      }
      return;
    case JoinInput::Kind::Address:
      view_ = inviteCode_.isEmpty() ? View::SignIn : View::Invite;
      connectAddress(in.address);
      return;
    case JoinInput::Kind::Link:
      openLink(text);
      return;
  }
}

void OnboardingController::securityMatches() {
  if (d_.conn->state() != State::NeedsTrustConfirmation) {
    return;
  }
  securityChecked_ = true;
  clearError();
  d_.conn->confirmTrust();
  emit changed();
}

void OnboardingController::securityDiffers() {
  if (d_.conn->state() != State::NeedsTrustConfirmation) {
    return;
  }
  mismatch_ = true;
  d_.conn->rejectTrust();  // nothing was saved
  leaveConnection();
  setError(QStringLiteral("security_code_mismatch"));
}

void OnboardingController::dismissMismatch() {
  mismatch_ = false;
  clearError();
  stage_ = Stage::Address;
  emit changed();
}

void OnboardingController::signIn(const QString& name, const QString& password) {
  const State s = d_.conn->state();
  if (s != State::NeedsPairing && s != State::Denied && s != State::Expired) {
    return;
  }
  clearError();
  busy_ = true;
  emit changed();
  d_.conn->signIn(name, password);
}

void OnboardingController::redeemInvite(const QString& code, const QString& name, const QString& password) {
  const State s = d_.conn->state();
  if (s != State::NeedsPairing && s != State::Denied && s != State::Expired) {
    return;
  }
  clearError();
  busy_ = true;
  emit changed();
  d_.conn->redeemInvite(code.trimmed().isEmpty() ? inviteCode_ : code, name, password);
}

void OnboardingController::haveInvite() {
  view_ = View::Invite;
  clearError();
  emit changed();
}

void OnboardingController::haveAccount() {
  view_ = View::SignIn;
  clearError();
  emit changed();
}

void OnboardingController::requestApproval() {
  clearError();
  d_.conn->requestPairing();
  emit changed();
}

void OnboardingController::cancelApproval() {
  d_.conn->cancelPairing();
  emit changed();
}

void OnboardingController::tryAgain() {
  clearError();
  d_.conn->retry();
  emit changed();
}

// ---------------------------------------------------------------- Set up

void OnboardingController::beginSetup() {
  if (stage_ == Stage::Active) {
    leaveConnection();
  }
  flow_ = QStringLiteral("setup");
  stage_ = Stage::Address;
  view_ = View::SignIn;
  mismatch_ = false;
  busy_ = false;
  setupCode_.clear();
  inviteCode_.clear();
  clearError();
  emit changed();
}

void OnboardingController::setupWithCode(const QString& address, const QString& setupCode) {
  if (stage_ == Stage::Active) {
    leaveConnection();
  }
  flow_ = QStringLiteral("setup");
  stage_ = Stage::Address;
  view_ = View::SignIn;
  mismatch_ = false;
  busy_ = false;
  clearError();
  const QString code = normalizeCode(setupCode);
  if (code.size() != 6 || !isCrockford(code)) {
    setError(QStringLiteral("invalid_setup_code"));
    return;
  }
  const JoinInput in = classifyJoinInput(address);
  if (in.kind != JoinInput::Kind::Address) {
    setError(QStringLiteral("invalid_address"));
    return;
  }
  setupCode_ = code;
  connectAddress(in.address);
}

void OnboardingController::createAdmin(const QString& name, const QString& password) {
  if (flow_ != QLatin1String("setup") || d_.conn->state() != State::NeedsPairing || setupCode_.isEmpty()) {
    return;
  }
  clearError();
  busy_ = true;
  emit changed();
  d_.conn->setupWithCode(setupCode_, name, password);
}

void OnboardingController::joinSetUpServer() {
  flow_ = QStringLiteral("join");
  view_ = View::SignIn;
  setupCode_.clear();
  clearError();
  emit changed();
}

void OnboardingController::beginSignInToCurrentServer() {
  const State s = d_.conn->state();
  if (s != State::NeedsPairing && s != State::Denied && s != State::Expired) {
    return;
  }
  flow_ = QStringLiteral("join");
  stage_ = Stage::Active;
  view_ = View::SignIn;
  profileExistedBefore_ = true;
  clearError();
  emit changed();
}

// ---------------------------------------------------------------- Links

bool OnboardingController::openLink(const QString& url) {
  const JoinInput in = classifyJoinInput(url);
  if (in.kind != JoinInput::Kind::Link && in.kind != JoinInput::Kind::BrokenLink) {
    return false;
  }
  if (in.kind == JoinInput::Kind::BrokenLink) {
    if (stage_ == Stage::Active) {
      leaveConnection();
    }
    flow_ = QStringLiteral("join");
    stage_ = Stage::Address;
    mismatch_ = false;
    setError(QStringLiteral("broken_link"));
    return true;
  }
  if (d_.gameRunning && d_.gameRunning()) {
    pendingLink_ = url;  // opens when the game ended (8c-3a); the invite code is never logged
    emit changed();
    return true;
  }
  if (stage_ == Stage::Active) {
    leaveConnection();
  }
  inviteCode_ = in.inviteCode;
  linkAddress_ = in.address;
  linkFingerprint_ = in.fingerprint;
  flow_ = QStringLiteral("link");
  mismatch_ = false;
  busy_ = false;
  clearError();
  if (d_.conn->state() == State::Connected) {
    stage_ = Stage::LinkChoice;  // 8c-2 (or 8c-3b when it is the open server)
    const auto linked = d_.profiles->profileByAddress(withScheme(linkAddress_));
    if (linked && linked->hubId == d_.conn->hubId()) {
      stage_ = Stage::LinkMember;
    }
    emit changed();
    return true;
  }
  startLink();
  return true;
}

void OnboardingController::startLink() {
  d_.conn->setExpectedFingerprint(linkFingerprint_);
  const auto known = d_.profiles->profileByAddress(withScheme(linkAddress_));
  profileExistedBefore_ = known.has_value();
  flow_ = QStringLiteral("link");
  typedAddress_ = linkAddress_;
  if (known && hasSignedInAccount(*known)) {
    stage_ = Stage::LinkMember;  // 8c-3b: nothing to redeem
    emit changed();
    return;
  }
  stage_ = Stage::Active;
  view_ = View::Invite;
  linkContinued_ = false;
  securityChecked_ = false;
  if (known && !known->accounts.isEmpty()) {
    d_.conn->beginAddAccount(known->hubId);
  } else {
    d_.conn->connectToAddress(linkAddress_, d_.allowHttp);
  }
  emit changed();
}

void OnboardingController::continueLink() {
  linkContinued_ = true;
  view_ = View::Invite;
  clearError();
  emit changed();
}

void OnboardingController::openLinkedServer() {
  if (stage_ != Stage::LinkMember) {
    return;
  }
  const auto known = d_.profiles->profileByAddress(withScheme(linkAddress_));
  if (!known) {
    return;
  }
  if (d_.conn->state() == State::Connected && d_.conn->hubId() == known->hubId) {
    complete();
    return;
  }
  d_.conn->setExpectedFingerprint(linkFingerprint_);
  profileExistedBefore_ = true;
  stage_ = Stage::Active;
  d_.conn->connectToProfile(known->hubId);
  emit changed();
}

void OnboardingController::confirmLink(bool addAndSwitch) {
  if (stage_ != Stage::LinkChoice) {
    return;
  }
  if (addAndSwitch) {
    if (d_.endRunningWork) {
      d_.endRunningWork();  // the running work of the open server ends before the connection goes
    }
    startLink();
  } else {
    addOnly();
  }
}

// "Add only": identify the linked server with its security code from the link and save it, without touching the open
// connection. The invite is not redeemed and not kept; the person opens the link again when they want to join.
void OnboardingController::addOnly() {
  probe_ = std::make_unique<HubConnection>(d_.profiles, d_.credentials);
  probe_->setExpectedFingerprint(linkFingerprint_);
  connect(probe_.get(), &HubConnection::stateChanged, this, [this](State s) {
    const bool done = s == State::NeedsPairing || s == State::Connected;
    const bool failed = s == State::Unreachable || s == State::Incompatible || s == State::CertificateChanged ||
                        s == State::Disconnected || s == State::NeedsTrustConfirmation;
    if (!done && !failed) {
      return;
    }
    const QString code = probe_->errorCode();
    const QString name = probe_->hubInfo().name;
    probe_.release()->deleteLater();
    if (done) {
      stage_ = Stage::Idle;
      flow_.clear();
      inviteCode_.clear();
      linkAddress_.clear();
      linkFingerprint_.clear();
      emit changed();
      emit linkAdded(name);
      return;
    }
    stage_ = Stage::Address;
    flow_ = QStringLiteral("join");
    if (code == QLatin1String("security_code_mismatch")) {
      mismatch_ = true;
    }
    setError(code.isEmpty() ? QStringLiteral("unreachable") : code);
  });
  probe_->connectToAddress(linkAddress_, d_.allowHttp);
}

void OnboardingController::dismissPendingLink() {
  pendingLink_.clear();
  emit changed();
}

void OnboardingController::gameEnded() {
  if (pendingLink_.isEmpty() || (d_.gameRunning && d_.gameRunning())) {
    return;
  }
  const QString link = std::exchange(pendingLink_, QString());
  openLink(link);
}

}  // namespace uzume::ui
