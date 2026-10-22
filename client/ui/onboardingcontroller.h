#pragma once
// OnboardingController (player.onboarding): the first start and every way onto a server, as one flow the "start" screen
// presents (0.18 package 7, docs/design/uzume/frame.md 8a, 8b, 8c): Play on this PC, Join a server (address, invite code
// or `uzume://` link), Set up a server with its setup code, the security check, sign-in with a password or an
// admin's approval, and the hard stops. It drives the one HubConnection of the app; there is no QML in here.
//
// QML contract
//   * Read `phase` (and `flow`) to choose what the start screen shows, `busy` for a request in flight, and
//     `errorCode` / `errorMessage` / `retryAfter` for the line under the form. The messages are the design's copy
//     (English); the codes are stable and listed below.
//   * Start a flow with beginJoin() / beginSetup() / submitJoinInput(); every other action is a method of the
//     current phase. cancel() leaves the flow from anywhere, back() goes one step back.
//   * `player.screen == "start"` while takesScreen() is true (see PlayerController). The first-start routing is off until
//     QML sets `startScreenEnabled` (so the older connection screens keep working meanwhile).
//
// Phases (`phase`), with the design boards they stand for
//   idle              nothing in progress
//   address           ask for the address / invite code / link (8b-1), or address + setup code (8a-4 step 2, flow "setup")
//   checking          contacting the server
//   security          security check (8b-2, 8a-4 step 3): securityCode, serverName, serverAddress -> securityMatches() /
//                     securityDiffers()
//   security_mismatch hard stop (8b-2 differs, 8c-3e, 8a-6f); nothing was sent and nothing saved -> dismissMismatch()
//   link_choice       8c-2: a link while another server is open -> confirmLink(true) add and switch, confirmLink(false) add
//                     only, cancel()
//   link_member       8c-3b: already a member of the linked server -> openLinkedServer()
//   link_ready        8c-1: "Security code matches", invited server shown -> continueLink() / cancel() (Not now)
//   invite            8b-3: redeemInvite(code, name, password); haveAccount() switches to sign_in
//   sign_in           8b-4 / 8b-6: signIn(name, password); haveInvite(), requestApproval()
//   approval_wait     8b-5h: waiting for the admin -> cancelApproval()
//   approval_denied   8b-5i -> requestApproval() again (the server allows a new request after a while)
//   approval_expired  the request ran out -> requestApproval()
//   setup_account     8a-4 step 4 / 8a-5: createAdmin(name, password)
//   setup_exists      8a-6d: the server is set up already -> joinSetUpServer() or back()
//   offline           8b-5e / 8c-3f: no answer -> tryAgain(), back() (change address)
//   server_too_old    8b-5f: the server must be updated (serverVersion)
//   app_too_old       the Uzume app must be updated
//   certificate_changed the pinned certificate of a known server changed (use the existing certificate-change handling)
//   disabled          8b-5d: the account is turned off -> back()
//   connected         done (the flow ends; `phase` is "idle" again right after)
//
// Error codes (`errorCode`; "" = none)
//   invalid_credentials, rate_limited (+retryAfter seconds), user_disabled, conflict, unreachable, hub_too_old, player_too_old,
//   invalid_input, bad_request, invite_invalid, display_name_taken, invalid_setup_code, setup_code_expired, already_set_up,
//   security_code_mismatch, certificate_changed, invalid_address, invalid_invite_code, broken_link, need_address,
//   insecure_http, invalid_hub_info, credential_store_failed, and the server's own code for anything else.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

#include "credentialstore.h"
#include "hubconnection.h"
#include "joinlink.h"
#include "playersettings.h"
#include "profilestore.h"

namespace uzume::ui {

class OnboardingController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")

  // No saved server, no library folder and "Play on this PC" not chosen yet (persisted in the player settings).
  Q_PROPERTY(bool firstStart READ firstStart NOTIFY changed)
  // Route the first start to the "start" screen (off by default; the start screen's QML switches it on).
  Q_PROPERTY(bool startScreenEnabled READ startScreenEnabled WRITE setStartScreenEnabled NOTIFY changed)
  // "" | "join" | "link" | "setup"
  Q_PROPERTY(QString flow READ flow NOTIFY changed)
  Q_PROPERTY(QString phase READ phase NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)  // a sign-in, invite or set-up request is in flight
  Q_PROPERTY(QString errorCode READ errorCode NOTIFY changed)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
  Q_PROPERTY(int retryAfter READ retryAfter NOTIFY changed)  // seconds, with rate_limited when the server named a wait
  // The server being joined or set up.
  Q_PROPERTY(QString serverName READ serverName NOTIFY changed)
  Q_PROPERTY(QString serverAddress READ serverAddress NOTIFY changed)  // host:port
  Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY changed)
  Q_PROPERTY(QString securityCode READ securityCode NOTIFY changed)    // K7Q4-9WMP-2HXD, of the certificate the server shows
  Q_PROPERTY(bool setupRequired READ setupRequired NOTIFY changed)     // the server has no admin yet
  // Invite code from a link or typed (UZ-XXXX-XXXX), to prefill the invite form; "" = none.
  Q_PROPERTY(QString inviteCode READ inviteCode NOTIFY changed)
  // Saved servers, most recently used first: [{hubId, name, address, lastUsed (ISO), selected}].
  Q_PROPERTY(QVariantList recentServers READ recentServers NOTIFY changed)
  Q_PROPERTY(QString selectedServerId READ selectedServerId NOTIFY changed)
  // A link opened while a game runs waits here until the game ended (8c-3a, shown as a toast in game); it then opens by itself.
  Q_PROPERTY(bool linkWaiting READ linkWaiting NOTIFY changed)
  // 8c-2 rows: the server that is open now (name, signed-in person) and the linked one (name, address).
  Q_PROPERTY(QVariantMap linkChoice READ linkChoice NOTIFY changed)
  // The linked server for 8c-1 / 8c-3b: {name, address, securityMatches, member}; empty map without a link.
  Q_PROPERTY(QVariantMap linkInfo READ linkInfo NOTIFY changed)

 public:
  struct Deps {
    HubConnection* conn = nullptr;
    ProfileStore* profiles = nullptr;
    CredentialStore* credentials = nullptr;
    PlayerSettings* settings = nullptr;
    bool allowHttp = false;
    std::function<bool()> gameRunning;           // a game or a watched Session is running
    std::function<void()> endRunningWork;        // saves and uploads the running game before the connection is dropped
    std::function<void(const QString&)> addLibraryFolder;  // path or file:// URL (PlayerController::addLibraryFolder)
    std::function<void()> startLocalHub;         // LocalHubController::start()
  };
  explicit OnboardingController(const Deps& deps, QObject* parent = nullptr);
  ~OnboardingController() override;

  bool firstStart() const;
  bool startScreenEnabled() const { return startScreenEnabled_; }
  void setStartScreenEnabled(bool on);
  QString flow() const { return flow_; }
  QString phase() const;
  bool busy() const { return busy_; }
  QString errorCode() const { return errorCode_; }
  QString errorMessage() const { return errorMessage_; }
  int retryAfter() const { return retryAfter_; }
  QString serverName() const;
  QString serverAddress() const;
  QString serverVersion() const;
  QString securityCode() const;
  bool setupRequired() const;
  QString inviteCode() const { return inviteCode_; }
  QVariantList recentServers() const;
  QString selectedServerId() const { return selectedHubId_; }
  bool linkWaiting() const { return !pendingLink_.isEmpty(); }
  QVariantMap linkChoice() const;
  QVariantMap linkInfo() const;

  // The start screen replaces the older connection screens (PlayerController::updateScreen).
  bool takesScreen() const;

  // ---- start ----
  // Leaves a flow from anywhere: drops a half-made connection (and a server saved only for it), keeps an open server.
  Q_INVOKABLE void cancel();
  // One step back: security -> address, invite -> sign_in, everything else -> address; from the address cancels.
  Q_INVOKABLE void back();

  // "Play on this PC" (8a-3): remembers the choice, starts the Hub on this PC (LocalHubController); addGameFolder() reuses the
  // library-folder flow (progress in player.libraryScan, result in player.libraryGameCount). finishPlayOnThisPc() is
  // "Start playing" / "Add later". Package 10 changes how that Hub runs; this is the existing flow.
  Q_INVOKABLE void choosePlayOnThisPc();
  Q_INVOKABLE void addGameFolder(const QString& pathOrUrl);
  Q_INVOKABLE void finishPlayOnThisPc();

  // ---- join a server ----
  Q_INVOKABLE void beginJoin();
  // Everything the person types or pastes in "Address or invite code" (8b-1): an address connects; an invite code goes to a
  // selected recent server, or asks for the address (errorCode need_address, the code is remembered); a link connects with the
  // link's security code (8c-1) and remembers the invite; broken or invalid input sets errorCode (broken_link,
  // invalid_address, invalid_invite_code). Empty text with a selected recent server connects to it.
  Q_INVOKABLE void submitJoinInput(const QString& text);
  Q_INVOKABLE void selectRecentServer(const QString& hubId);  // empty clears
  // The security check (8b-2). securityMatches() remembers the certificate and goes on; securityDiffers() is a hard stop
  // (phase security_mismatch, nothing sent, nothing saved).
  Q_INVOKABLE void securityMatches();
  Q_INVOKABLE void securityDiffers();
  Q_INVOKABLE void dismissMismatch();  // back to the address step
  Q_INVOKABLE void signIn(const QString& name, const QString& password);
  // `code` empty = the remembered invite code. `password` optional.
  Q_INVOKABLE void redeemInvite(const QString& code, const QString& name, const QString& password);
  Q_INVOKABLE void haveInvite();    // sign_in -> invite
  Q_INVOKABLE void haveAccount();   // invite -> sign_in ("I already have an account")
  Q_INVOKABLE void requestApproval();  // no password: ask an admin to approve this PC
  Q_INVOKABLE void cancelApproval();
  Q_INVOKABLE void tryAgain();      // offline / unreachable: identify again

  // ---- set up a server ----
  // Phase address with flow "setup": address and setup code together (8a-4 step 2). A code of the wrong shape is
  // invalid_setup_code at once; the code is held in memory only, sent by createAdmin() after the security check.
  Q_INVOKABLE void beginSetup();
  Q_INVOKABLE void setupWithCode(const QString& address, const QString& setupCode);
  // Step 4 (8a-5): the admin account. "Server name" and "Who can join" have no call behind them in setupWithCode, they are not
  // part of this flow (set them in the server's settings afterwards).
  Q_INVOKABLE void createAdmin(const QString& name, const QString& password);
  Q_INVOKABLE void joinSetUpServer();  // 8a-6d "Join <server>"

  // ---- invite links ----
  // `uzume://join/...` from the command line, the single-instance socket or Ctrl+V. False when `url` is not an `uzume://`
  // link at all (nothing happens); a broken one sets errorCode broken_link. While a game runs the link waits (linkWaiting).
  Q_INVOKABLE bool openLink(const QString& url);
  Q_INVOKABLE void continueLink();       // 8c-1 Continue
  Q_INVOKABLE void openLinkedServer();   // 8c-3b
  Q_INVOKABLE void confirmLink(bool addAndSwitch);  // 8c-2
  Q_INVOKABLE void dismissPendingLink();
  // PlayerController: no game runs any more -> a waiting link opens.
  void gameEnded();

  // The person who just removed the last account of the open server or lost the session lands here to sign in again.
  Q_INVOKABLE void beginSignInToCurrentServer();

  // Re-evaluates firstStart and the recent servers (profiles or folders changed).
  void refresh();

 signals:
  void changed();
  void linkAdded(const QString& serverName);  // confirmLink(false) saved the server

 private:
  enum class Stage { Idle, Address, Active, LinkChoice, LinkMember };
  enum class View { SignIn, Invite };

  void onState(HubConnection::State s);
  void onError(const QString& code, const QString& message);
  void setError(const QString& code, const QString& message = QString(), int retryAfter = 0);
  void clearError();
  QString messageFor(const QString& code, const QString& serverMessage, int retryAfter) const;
  void connectAddress(const QString& address);
  void connectKnownServer(const QString& hubId);
  void startLink();
  void complete();
  void leaveConnection();
  void addOnly();
  static QString displayAddress(const QString& address);
  static QString durationText(int seconds);

  Deps d_;
  bool startScreenEnabled_ = false;
  Stage stage_ = Stage::Idle;
  View view_ = View::SignIn;
  QString flow_;
  bool busy_ = false;
  bool mismatch_ = false;
  QString errorCode_;
  QString errorMessage_;
  int retryAfter_ = 0;
  QString inviteCode_;
  QString setupCode_;     // memory only
  bool securityChecked_ = false;
  QString selectedHubId_;
  QString typedAddress_;  // what the person asked for, for messages before the server answered
  bool profileExistedBefore_ = false;
  QString linkAddress_;
  QString linkFingerprint_;
  bool linkContinued_ = false;
  QString pendingLink_;
  std::unique_ptr<HubConnection> probe_;  // "Add only": identifies the linked server without touching the open connection
  QString probeName_;
};

}  // namespace uzume::ui
