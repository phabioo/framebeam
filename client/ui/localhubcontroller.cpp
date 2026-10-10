#include "localhubcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFutureWatcher>
#include <QTimer>
#include "hubhttp.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QThread>
#include <QUrl>
#include <QtConcurrent>

namespace framebeam::ui {

using S = HubConnection::State;

LocalHubController::LocalHubController(const Deps& deps, QObject* parent)
    : QObject(parent), deps_(deps), installer_(deps.installer), info_(localhub::detect()) {
  grant_ = [](const QString& program, const QStringList& args, QString* err) {
    return localhub::runElevatedAndWait(program, args, err);
  };
  connect(deps_.conn, &HubConnection::stateChanged, this, &LocalHubController::onState);
  connect(deps_.conn, &HubConnection::errorOccurred, this, &LocalHubController::onError);
  connect(&installer_, &update::HubSetupInstaller::changed, this, [this]() {
    if (installer_.state() == update::HubSetupInstaller::State::Failed) {
      setPhase(QStringLiteral("error"), installer_.error());
    } else {
      emit changed();
    }
  });
  connect(&installer_, &update::HubSetupInstaller::quitRequested, this, &LocalHubController::quitRequested);
}

bool LocalHubController::available() const { return info_.present() || localhub::installSupported(); }

QString LocalHubController::installText() const {
  using St = update::HubSetupInstaller::State;
  switch (installer_.state()) {
    case St::Fetching: return tr("Looking up the installer…");
    case St::Downloading: return tr("Downloading the installer…");
    case St::Launching: return tr("Waiting for Windows to ask for administrator permission…");
    default: return {};
  }
}

bool LocalHubController::onThisPc() const {
  return info_.present() && deps_.conn->state() == S::Connected && localhub::isLocalHubAddress(deps_.conn->address(), info_.port);
}

QString LocalHubController::hubWebUrl() const { return info_.present() ? localhub::hubUrl(info_.port) + QStringLiteral("/") : QString(); }

void LocalHubController::setPhase(const QString& p, const QString& err) {
  phase_ = p;
  error_ = err;
  emit changed();
}

QString LocalHubController::friendly(const QString& code, const QString& message) {
  if (code == QLatin1String("rate_limited")) return tr("Too many attempts. Please wait a moment and try again.");
  if (code == QLatin1String("invalid_credentials") || code == QLatin1String("unauthorized")) {
    return tr("User name or password is wrong, or this user is not a Hub admin.");
  }
  if (code == QLatin1String("admin_exists")) return tr("This Hub already has an admin. Sign in instead.");
  if (code == QLatin1String("not_local")) return tr("This is only possible with the Hub on this PC.");
  if (code == QLatin1String("invalid_input")) return tr("Please enter a user name and a password.");
  if (code == QLatin1String("unreachable")) return tr("The Hub on this PC does not respond. Check that the Windows service \"FrameBeam Hub\" is running.");
  return message.isEmpty() ? tr("The Hub refused the request (%1).").arg(code) : message;
}

void LocalHubController::start() {
  if (phase_ == QLatin1String("installing") || phase_ == QLatin1String("connecting") || phase_ == QLatin1String("working")) return;
  info_ = localhub::detect();
  error_.clear();
  if (!info_.present()) {
    if (!localhub::installSupported()) {
      setPhase(QStringLiteral("error"), tr("Setting up a Hub from the Player is only available on Windows."));
      return;
    }
    setPhase(QStringLiteral("installing"));
    installer_.start();
    return;
  }
  active_ = true;
  setPhase(QStringLiteral("connecting"));
  deps_.addHub(localhub::hubUrl(info_.port));
}

void LocalHubController::startAfterInstall() {
  retries_ = 20;
  start();
}

void LocalHubController::cancel() {
  if (phase_ == QLatin1String("installing")) installer_.cancel();
  active_ = false;
  if (phase_ == QLatin1String("connecting") || phase_ == QLatin1String("needsSetup") || phase_ == QLatin1String("needsSignIn") ||
      phase_ == QLatin1String("working")) {
    deps_.conn->disconnectFromHub();
  }
  setPhase(QStringLiteral("idle"));
}

void LocalHubController::onState(S s) {
  if (s == S::Connected && onThisPc()) {
    info_ = localhub::detect();
    const bool fresh = active_;
    if (active_) setPhase(QStringLiteral("idle"));
    active_ = false;
    refreshStatus();
    emit changed();
    if (fresh) emit setupCompleted();
    return;
  }
  if (!active_) {
    emit changed();
    return;
  }
  switch (s) {
    case S::NeedsTrustConfirmation:
      // Only the loopback address of the registered Hub: pinned as if the user had accepted it (self-signed cert).
      if (localhub::isLocalHubAddress(deps_.conn->address(), info_.port) && !deps_.conn->observedFingerprint().isEmpty()) {
        deps_.conn->confirmTrust();
      } else {
        active_ = false;
        setPhase(QStringLiteral("error"), tr("The address does not match the Hub on this PC."));
      }
      break;
    case S::NeedsPairing:
    case S::Denied:
    case S::Expired:
      if (phase_ == QLatin1String("connecting") || phase_ == QLatin1String("working")) loadStatus(true);
      break;
    case S::Unreachable:
      if (retries_ > 0) {  // the Hub service was just installed and may still be starting
        --retries_;
        QTimer::singleShot(1500, this, [this]() {
          if (active_ && deps_.conn->state() == S::Unreachable) deps_.addHub(localhub::hubUrl(info_.port));
        });
        break;
      }
      active_ = false;
      setPhase(QStringLiteral("error"), friendly(QStringLiteral("unreachable"), {}));
      break;
    case S::CertificateChanged:
    case S::Incompatible:
    case S::UserDisabled:
      active_ = false;
      setPhase(QStringLiteral("error"), tr("The Player could not connect to the Hub on this PC. Use the Hubs settings to check it."));
      break;
    case S::Disconnected:
      if (phase_ != QLatin1String("idle") && phase_ != QLatin1String("error")) {
        active_ = false;
        setPhase(QStringLiteral("idle"));
      }
      break;
    default:
      emit changed();
      break;
  }
}

void LocalHubController::onError(const QString& code, const QString& message) {
  if (!active_ || code == QLatin1String("unreachable")) return;  // unreachable is handled by the state change
  if (phase_ == QLatin1String("working")) {
    setPhase(setupForm_ ? QStringLiteral("needsSetup") : QStringLiteral("needsSignIn"), friendly(code, message));  // form stays open
  } else if (phase_ == QLatin1String("needsSetup") || phase_ == QLatin1String("needsSignIn")) {
    error_ = friendly(code, message);
    emit changed();
  }
}

void LocalHubController::loadStatus(bool forPairingForm) {
  deps_.conn->fetchLocalStatus([this, forPairingForm](bool ok, const QJsonObject& st, const QString& why) {
    if (!ok) {
      if (!forPairingForm) return;
      active_ = false;
      setPhase(QStringLiteral("error"),
               why == QLatin1String("not_local") ? friendly(why, {})
                                                 : tr("This Hub cannot be set up from the Player. Update FrameBeam Hub or add it as a normal hub."));
      return;
    }
    applyStatus(st);
    if (forPairingForm) {
      setupForm_ = !st.value(QStringLiteral("admin_exists")).toBool();
      setPhase(setupForm_ ? QStringLiteral("needsSetup") : QStringLiteral("needsSignIn"));
    }
  });
}

void LocalHubController::applyStatus(const QJsonObject& st) {
  networkSharing_ = st.value(QStringLiteral("network_sharing")).toBool();
  importDir_ = st.value(QStringLiteral("import_dir")).toString();
  emit changed();
}

void LocalHubController::refreshStatus() {
  if (!onThisPc()) return;
  loadStatus(false);
}

void LocalHubController::submit(const QString& username, const QString& password, const QString& passwordAgain) {
  if (phase_ != QLatin1String("needsSetup") && phase_ != QLatin1String("needsSignIn")) return;
  if (username.trimmed().isEmpty() || password.isEmpty()) {
    error_ = friendly(QStringLiteral("invalid_input"), {});
    emit changed();
    return;
  }
  if (phase_ == QLatin1String("needsSetup") && password != passwordAgain) {
    error_ = tr("The two passwords are not the same.");
    emit changed();
    return;
  }
  const bool setup = phase_ == QLatin1String("needsSetup");
  setupForm_ = setup;
  setPhase(QStringLiteral("working"));
  if (setup) {
    deps_.conn->localSetup(username, password);
  } else {
    deps_.conn->localPair(username, password);
  }
}

void LocalHubController::putSettings(const QJsonObject& body, const std::function<void(bool, const QString&)>& done) {
  QNetworkReply* reply = deps_.conn->authorizedSend("PUT", QStringLiteral("/local/settings"),
                                                    QJsonDocument(body).toJson(QJsonDocument::Compact), {}, "application/json");
  if (reply == nullptr) {
    done(false, tr("The Hub on this PC is not connected."));
    return;
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    if (r.ok()) {
      applyStatus(r.json());
      done(true, {});
    } else if (r.networkError) {
      // A change of network sharing restarts the Hub's server; the answer may be lost. The status tells the result.
      done(false, QString());
    } else {
      done(false, r.apiErrorMessage.isEmpty() ? friendly(r.apiErrorCode, {}) : r.apiErrorMessage);
    }
  });
}

void LocalHubController::setNetworkSharing(bool on) {
  if (!onThisPc() || settingsBusy_) return;
  settingsBusy_ = true;
  settingsError_.clear();
  settingsNotice_.clear();
  emit changed();
  putSettings({{QStringLiteral("network_sharing"), on}}, [this, on](bool ok, const QString& why) {
    settingsBusy_ = false;
    if (!ok && !why.isEmpty()) {
      settingsError_ = why;
    } else if (!ok) {
      networkSharing_ = on;  // answer lost while the server restarted; refreshStatus() corrects it if needed
      QTimer::singleShot(2000, this, &LocalHubController::refreshStatus);
    }
    emit changed();
  });
}

void LocalHubController::chooseFolder(const QString& pathOrUrl) {
  if (!onThisPc() || settingsBusy_) return;
  QString path = pathOrUrl.trimmed();
  if (path.startsWith(QLatin1String("file:"))) path = QUrl(path).toLocalFile();
  path = QDir::toNativeSeparators(path);
  if (path.isEmpty()) return;
  settingsBusy_ = true;
  settingsError_.clear();
  settingsNotice_.clear();
  emit changed();
  const auto save = [this, path]() {
    putSettings({{QStringLiteral("import_dir"), path}}, [this](bool ok, const QString& why) {
      settingsBusy_ = false;
      if (ok) {
        settingsNotice_ = tr("Folder saved. Open the Hub web page and choose \"Rescan folder\" to import the games.");
      } else {
        settingsError_ = why.isEmpty() ? tr("The Hub did not answer.") : why;
      }
      emit changed();
    });
  };
  if (!localhub::installSupported() || info_.installDir.isEmpty()) {
    save();  // no Windows service to grant access to (development): the Hub checks the folder itself
    return;
  }
  const localhub::Command cmd = localhub::grantFolderCommand(info_.installDir, path);
  const auto runner = grant_;
  auto* watcher = new QFutureWatcher<QPair<int, QString>>(this);
  connect(watcher, &QFutureWatcher<QPair<int, QString>>::finished, this, [this, watcher, save]() {
    const QPair<int, QString> res = watcher->result();
    watcher->deleteLater();
    if (res.first != 0) {
      settingsBusy_ = false;
      settingsError_ = res.first < 0 ? res.second
                                     : tr("Windows could not give the Hub access to this folder (code %1).").arg(res.first);
      emit changed();
      return;
    }
    save();
  });
  watcher->setFuture(QtConcurrent::run([runner, cmd]() {
    QString err;
    const int code = runner(cmd.program, cmd.args, &err);
    return QPair<int, QString>(code, err);
  }));
}

void LocalHubController::openHubWebUi() {
  if (info_.present()) QDesktopServices::openUrl(QUrl(hubWebUrl()));
}

}  // namespace framebeam::ui
