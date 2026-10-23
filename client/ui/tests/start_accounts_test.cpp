// OnboardingController and AccountsController against the fake server: join by address with the security check,
// sign-in, link with a wrong security code, set-up with a code, switching people with the PIN. No window.
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "accountscontroller.h"
#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "joinlink.h"
#include "onboardingcontroller.h"
#include "playersettings.h"
#include "profilestore.h"

using namespace uzume;
using namespace uzume::ui;

#define WAIT_PHASE(ctl, p) QTRY_COMPARE_WITH_TIMEOUT((ctl)->phase(), QStringLiteral(p), 8000)

namespace {
const QString kPassword = QStringLiteral("correct horse battery");
}

class StartAccountsTest : public QObject {
  Q_OBJECT
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<PlayerSettings> settings_;
  std::unique_ptr<HubConnection> conn_;
  std::unique_ptr<OnboardingController> onboarding_;
  std::unique_ptr<AccountsController> accounts_;
  bool gameRunning_ = false;
  int quitCalls_ = 0;

 private slots:
  void init() {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->path());
    creds_ = std::make_unique<MemoryCredentialStore>();
    settings_ = std::make_unique<PlayerSettings>(dir_->path());
    conn_ = std::make_unique<HubConnection>(profiles_.get(), creds_.get());
    conn_->setPollIntervalMs(50);
    gameRunning_ = false;
    quitCalls_ = 0;
    OnboardingController::Deps od;
    od.conn = conn_.get();
    od.profiles = profiles_.get();
    od.credentials = creds_.get();
    od.settings = settings_.get();
    od.gameRunning = [this] { return gameRunning_; };
    od.endRunningWork = [this] { ++quitCalls_; gameRunning_ = false; };
    onboarding_ = std::make_unique<OnboardingController>(od);
    AccountsController::Deps ad;
    ad.conn = conn_.get();
    ad.profiles = profiles_.get();
    ad.credentials = creds_.get();
    ad.gameRunning = od.gameRunning;
    ad.endRunningWork = od.endRunningWork;
    accounts_ = std::make_unique<AccountsController>(ad);
  }
  void cleanup() {
    accounts_.reset();
    onboarding_.reset();
    conn_.reset();
    settings_.reset();
    creds_.reset();
    profiles_.reset();
    dir_.reset();
  }

  void firstStartUntilSomethingIsSaved() {
    QVERIFY(onboarding_->firstStart());
    QCOMPARE(onboarding_->phase(), QStringLiteral("idle"));
    QVERIFY(!onboarding_->takesScreen());  // routing is opt-in
    onboarding_->setStartScreenEnabled(true);
    QVERIFY(onboarding_->takesScreen());
    onboarding_->choosePlayOnThisPc();
    QVERIFY(!onboarding_->firstStart());
  }

  void playStageKeepsTheStartScreenUntilFinished() {
    onboarding_->setStartScreenEnabled(true);
    onboarding_->beginPlay();
    QCOMPARE(onboarding_->flow(), QStringLiteral("play"));
    QCOMPARE(onboarding_->phase(), QStringLiteral("play"));
    QVERIFY(onboarding_->takesScreen());
    // Saving the "Play on this PC" choice early (a folder was added) does not drop the screen.
    settings_->setPlayOnThisPcChosen(true);
    QVERIFY(!onboarding_->firstStart());
    QVERIFY(onboarding_->takesScreen());
    onboarding_->back();  // cancel
    QCOMPARE(onboarding_->phase(), QStringLiteral("idle"));
    QVERIFY(onboarding_->flow().isEmpty());
    onboarding_->beginPlay();
    onboarding_->finishPlayOnThisPc();
    QVERIFY(!onboarding_->firstStart());
    QVERIFY(!onboarding_->takesScreen());
    QCOMPARE(onboarding_->phase(), QStringLiteral("idle"));
  }

  void joinByAddressWithSecurityCheckAndSignIn() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    onboarding_->beginJoin();
    QCOMPARE(onboarding_->phase(), QStringLiteral("address"));
    onboarding_->submitJoinInput(hub.address());
    WAIT_PHASE(onboarding_, "security");
    QCOMPARE(onboarding_->securityCode().size(), 14);  // XXXX-XXXX-XXXX
    onboarding_->securityMatches();
    WAIT_PHASE(onboarding_, "sign_in");
    onboarding_->signIn(QStringLiteral("anna"), QStringLiteral("wrong password!!"));
    QTRY_COMPARE_WITH_TIMEOUT(onboarding_->errorCode(), QStringLiteral("invalid_credentials"), 8000);
    QCOMPARE(onboarding_->phase(), QStringLiteral("sign_in"));
    onboarding_->signIn(QStringLiteral("anna"), kPassword);
    WAIT_PHASE(onboarding_, "idle");
    QVERIFY(conn_->state() == HubConnection::State::Connected);
    QCOMPARE(profiles_->accounts(hub.hubId).size(), 1);
  }

  void securityDiffersIsAHardStopAndSavesNothing() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    onboarding_->beginJoin();
    onboarding_->submitJoinInput(hub.address());
    WAIT_PHASE(onboarding_, "security");
    onboarding_->securityDiffers();
    QCOMPARE(onboarding_->phase(), QStringLiteral("security_mismatch"));
    QVERIFY(profiles_->profiles().isEmpty());
    onboarding_->dismissMismatch();
    QCOMPARE(onboarding_->phase(), QStringLiteral("address"));
  }

  void linkWithAWrongSecurityCodeSendsNothing() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    const QString address = hub.address().mid(QStringLiteral("https://").size());
    const QString wrong = QString(64, QLatin1Char('A'));
    QVERIFY(onboarding_->openLink(makeJoinLink(address, QStringLiteral("UZ-8XK2-M4QD"), wrong)));
    WAIT_PHASE(onboarding_, "security_mismatch");
    QCOMPARE(onboarding_->errorCode(), QStringLiteral("security_code_mismatch"));
    QVERIFY(profiles_->profiles().isEmpty());
    for (const FakeRequest& r : hub.requests) {
      QVERIFY2(!r.path.contains(QStringLiteral("invite")), "nothing may be sent after a mismatch");
    }
  }

  void brokenLinkAndWaitingLink() {
    QVERIFY(!onboarding_->openLink(QStringLiteral("https://example.org")));
    QVERIFY(onboarding_->openLink(QStringLiteral("uzume://join/")));
    QCOMPARE(onboarding_->errorCode(), QStringLiteral("broken_link"));
    gameRunning_ = true;
    QVERIFY(onboarding_->openLink(QStringLiteral("uzume://join/pi.local/UZ-8XK2-M4QD#") + QString(64, QLatin1Char('B'))));
    QVERIFY(onboarding_->linkWaiting());
    onboarding_->dismissPendingLink();
    QVERIFY(!onboarding_->linkWaiting());
  }

  void setupWithACode() {
    FakeHub hub(QStringLiteral("a"));
    hub.setupRequired = true;
    QVERIFY(hub.start());
    onboarding_->beginSetup();
    onboarding_->setupWithCode(hub.address(), QStringLiteral("zzz"));
    QCOMPARE(onboarding_->errorCode(), QStringLiteral("invalid_setup_code"));
    onboarding_->setupWithCode(hub.address(), QStringLiteral("7H4K2Q"));
    WAIT_PHASE(onboarding_, "security");
    onboarding_->securityMatches();
    WAIT_PHASE(onboarding_, "setup_account");
    onboarding_->createAdmin(QStringLiteral("admin"), kPassword);
    WAIT_PHASE(onboarding_, "idle");
    QVERIFY(conn_->state() == HubConnection::State::Connected);
  }

  void switchPeopleWithPinAndAddAPerson() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    hub.addAccount(QStringLiteral("ben"), kPassword);
    onboarding_->beginJoin();
    onboarding_->submitJoinInput(hub.address());
    WAIT_PHASE(onboarding_, "security");
    onboarding_->securityMatches();
    WAIT_PHASE(onboarding_, "sign_in");
    onboarding_->signIn(QStringLiteral("anna"), kPassword);
    WAIT_PHASE(onboarding_, "idle");

    QCOMPARE(accounts_->people().size(), 1);
    accounts_->open();
    accounts_->addPerson();
    QCOMPARE(accounts_->phase(), QStringLiteral("add"));
    QVERIFY(accounts_->takesScreen() || conn_->state() == HubConnection::State::Connected);
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == HubConnection::State::NeedsPairing, 8000);
    accounts_->addSignIn(QStringLiteral("ben"), kPassword);
    WAIT_PHASE(accounts_, "idle");
    QCOMPARE(accounts_->people().size(), 2);
    QCOMPARE(accounts_->people().first().toMap().value(QStringLiteral("name")).toString().isEmpty(), false);

    // A PIN for Ben (the active person), then switch to Anna and back with the PIN.
    QVERIFY(accounts_->setPin(QStringLiteral("4821")));
    accounts_->open();
    accounts_->switchTo(QStringLiteral("u_anna"));
    WAIT_PHASE(accounts_, "idle");
    QCOMPARE(conn_->hubUserId(), QStringLiteral("u_anna"));
    accounts_->open();
    accounts_->switchTo(QStringLiteral("u_ben"));
    QCOMPARE(accounts_->phase(), QStringLiteral("pin"));
    accounts_->enterPin(QStringLiteral("0000"));
    QCOMPARE(accounts_->pinError(), QStringLiteral("wrong"));
    QCOMPARE(accounts_->pinTriesLeft(), 2);
    accounts_->enterPin(QStringLiteral("4821"));
    WAIT_PHASE(accounts_, "idle");
    QCOMPARE(conn_->hubUserId(), QStringLiteral("u_ben"));
  }

  void switchingWithAGameRunningSavesFirst() {
    FakeHub hub(QStringLiteral("a"));
    QVERIFY(hub.start());
    hub.addAccount(QStringLiteral("anna"), kPassword);
    hub.addAccount(QStringLiteral("ben"), kPassword);
    onboarding_->beginJoin();
    onboarding_->submitJoinInput(hub.address());
    WAIT_PHASE(onboarding_, "security");
    onboarding_->securityMatches();
    WAIT_PHASE(onboarding_, "sign_in");
    onboarding_->signIn(QStringLiteral("anna"), kPassword);
    WAIT_PHASE(onboarding_, "idle");
    accounts_->addPerson();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == HubConnection::State::NeedsPairing, 8000);
    accounts_->addSignIn(QStringLiteral("ben"), kPassword);
    WAIT_PHASE(accounts_, "idle");

    gameRunning_ = true;
    accounts_->open();
    accounts_->switchTo(QStringLiteral("u_anna"));
    QCOMPARE(accounts_->phase(), QStringLiteral("quit_game"));
    QCOMPARE(quitCalls_, 0);
    accounts_->keepPlaying();
    QCOMPARE(accounts_->phase(), QStringLiteral("people"));
    accounts_->switchTo(QStringLiteral("u_anna"));
    accounts_->confirmQuitAndSwitch();
    QCOMPARE(quitCalls_, 1);
    WAIT_PHASE(accounts_, "idle");
    QCOMPARE(conn_->hubUserId(), QStringLiteral("u_anna"));
  }
};

QTEST_MAIN(StartAccountsTest)
#include "start_accounts_test.moc"
