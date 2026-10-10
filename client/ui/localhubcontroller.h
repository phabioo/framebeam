#pragma once
// LocalHubController: "Set up a Hub on this PC" (0.9 "One installer"). Installs the Hub when none is registered
// (Windows MSI), connects to the Hub on this PC over loopback (its self-signed certificate is trusted automatically for
// exactly this address), creates the first Hub admin or signs in as an existing admin, and edits the Hub's local
// settings (network sharing, ROM folder) afterwards.

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

#include "hubconnection.h"
#include "hubsetup.h"
#include "localhub.h"

namespace framebeam::ui {

class LocalHubController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by PlayerController")

  // The button is shown: a Hub is registered on this PC, or the Player can install one (Windows).
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(bool present READ present NOTIFY changed)
  // idle | installing | connecting | needsSetup | needsSignIn | working | error
  Q_PROPERTY(QString phase READ phase NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QString installText READ installText NOTIFY changed)
  Q_PROPERTY(double installProgress READ installProgress NOTIFY changed)
  // The active Hub is the Hub on this PC: "This PC's Hub" card.
  Q_PROPERTY(bool onThisPc READ onThisPc NOTIFY changed)
  Q_PROPERTY(bool networkSharing READ networkSharing NOTIFY changed)
  Q_PROPERTY(QString importDir READ importDir NOTIFY changed)
  Q_PROPERTY(QString settingsError READ settingsError NOTIFY changed)
  Q_PROPERTY(QString settingsNotice READ settingsNotice NOTIFY changed)
  Q_PROPERTY(bool settingsBusy READ settingsBusy NOTIFY changed)
  Q_PROPERTY(QString hubWebUrl READ hubWebUrl NOTIFY changed)

 public:
  struct Deps {
    HubConnection* conn = nullptr;
    // Starts the normal "add hub" attempt for the address (PlayerController::addHub).
    std::function<void(const QString&)> addHub;
    update::HubSetupInstaller::Config installer;
  };
  explicit LocalHubController(const Deps& deps, QObject* parent = nullptr);

  bool available() const;
  bool present() const { return info_.present(); }
  QString phase() const { return phase_; }
  QString error() const { return error_; }
  QString installText() const;
  double installProgress() const { return installer_.progress(); }
  bool onThisPc() const;
  bool networkSharing() const { return networkSharing_; }
  QString importDir() const { return importDir_; }
  QString settingsError() const { return settingsError_; }
  QString settingsNotice() const { return settingsNotice_; }
  bool settingsBusy() const { return settingsBusy_; }
  QString hubWebUrl() const;

  update::HubSetupInstaller* installer() { return &installer_; }
  // Tests: replaces the elevated `grant-folder` call (exit code, error text).
  void setGrantRunner(std::function<int(const QString& program, const QStringList& args, QString* error)> f) { grant_ = std::move(f); }

  // Button "Set up a Hub on this PC" (also `--setup-local-hub` after the MSI install).
  Q_INVOKABLE void start();
  // Like start(), but waits up to ~30 s for the freshly installed Hub service (`--setup-local-hub`).
  Q_INVOKABLE void startAfterInstall();
  Q_INVOKABLE void cancel();
  // Form of the pairing screen: first admin (needsSetup, password twice) or an existing admin (needsSignIn).
  Q_INVOKABLE void submit(const QString& username, const QString& password, const QString& passwordAgain);
  Q_INVOKABLE void setNetworkSharing(bool on);
  // Folder from the picker (path or file:// URL): grants the Hub service read access (Windows, UAC) and saves it.
  Q_INVOKABLE void chooseFolder(const QString& pathOrUrl);
  Q_INVOKABLE void openHubWebUi();
  Q_INVOKABLE void refreshStatus();

 signals:
  void changed();
  void setupCompleted();  // paired and connected to the Hub on this PC
  void quitRequested();  // installer started: the Player exits

 private:
  void onState(HubConnection::State s);
  void onError(const QString& code, const QString& message);
  void loadStatus(bool forPairingForm);
  void applyStatus(const QJsonObject& st);
  void putSettings(const QJsonObject& body, const std::function<void(bool, const QString&)>& done);
  void setPhase(const QString& p, const QString& err = {});
  static QString friendly(const QString& code, const QString& message);

  Deps deps_;
  update::HubSetupInstaller installer_;
  localhub::Info info_;
  int retries_ = 0;
  bool setupForm_ = false;  // the last form was "Create the Hub admin"
  bool active_ = false;  // a "Set up a Hub on this PC" attempt is running: its loopback address is trusted automatically
  QString phase_ = QStringLiteral("idle");
  QString error_;
  bool networkSharing_ = false;
  QString importDir_;
  QString settingsError_;
  QString settingsNotice_;
  bool settingsBusy_ = false;
  std::function<int(const QString&, const QStringList&, QString*)> grant_;
};

}  // namespace framebeam::ui
