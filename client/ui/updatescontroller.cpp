#include "updatescontroller.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QLocale>
#include <QUrl>

#include "version.h"

namespace framebeam::ui {

using update::UpdateManager;

namespace {

UpdateManager::Config makeConfig(const UpdatesController::Options& o) {
  UpdateManager::Config c;
  c.baseDir = o.baseDir;
  c.indexUrl = o.indexUrl;
  c.currentVersion = QString::fromUtf8(playerVersion().data(), static_cast<qsizetype>(playerVersion().size()));
  c.compiledChannel = QString::fromUtf8(playerChannel().data(), static_cast<qsizetype>(playerChannel().size()));
  c.installRoot = o.installRoot;
  c.forceApplySupport = o.forceApplySupport;
  return c;
}

}  // namespace

UpdatesController::UpdatesController(const Options& options, PlayerSettings* settings, QObject* parent)
    : QObject(parent), manager_(makeConfig(options), settings), settings_(settings) {
  connect(&manager_, &UpdateManager::changed, this, &UpdatesController::changed);
  connect(&manager_, &UpdateManager::quitRequested, this, &UpdatesController::quitRequested);
}

void UpdatesController::setProviders(std::function<std::optional<update::HubProtocol>()> hub, std::function<bool()> busy) {
  manager_.setHubProtocolProvider(std::move(hub));
  manager_.setBusyProvider(std::move(busy));
}

QString UpdatesController::compiledChannel() const {
  return QString::fromUtf8(playerChannel().data(), static_cast<qsizetype>(playerChannel().size()));
}

QString UpdatesController::channelSetting() const {
  const QString c = settings_->updateChannel();
  return c.isEmpty() ? QStringLiteral("default") : c;
}

QString UpdatesController::effectiveChannel() const { return update::channelName(manager_.effectiveChannel()); }

QString UpdatesController::state() const {
  switch (manager_.state()) {
    case UpdateManager::State::Idle: return QStringLiteral("idle");
    case UpdateManager::State::Disabled: return QStringLiteral("disabled");
    case UpdateManager::State::Checking: return QStringLiteral("checking");
    case UpdateManager::State::UpToDate: return QStringLiteral("up_to_date");
    case UpdateManager::State::Available: return QStringLiteral("available");
    case UpdateManager::State::Incompatible: return QStringLiteral("incompatible");
    case UpdateManager::State::Downloading: return QStringLiteral("downloading");
    case UpdateManager::State::Ready: return QStringLiteral("ready");
    case UpdateManager::State::Applying: return QStringLiteral("applying");
    case UpdateManager::State::Error: return QStringLiteral("error");
  }
  return QStringLiteral("idle");
}

QString UpdatesController::statusText() const {
  if (manager_.state() == UpdateManager::State::Idle) {
    return manager_.effectiveChannel() == update::Channel::Off
               ? tr("Updates are off for this development build. Choose a channel to check for updates.")
               : tr("Not checked yet.");
  }
  return manager_.statusText();
}

QString UpdatesController::lastCheckText() const {
  const QDateTime t = manager_.lastCheck();
  return t.isValid() ? QLocale().toString(t, QLocale::ShortFormat) : tr("never");
}

bool UpdatesController::updateAvailable() const {
  if (manager_.availableVersion().isEmpty()) return false;
  switch (manager_.state()) {
    case UpdateManager::State::Available:
    case UpdateManager::State::Downloading:
    case UpdateManager::State::Ready:
    case UpdateManager::State::Applying: return true;
    default: return false;
  }
}

// Cadence of the periodic check (core/updater.cpp: timer 1 h on Beta, 24 h on Stable).
QString UpdatesController::cadenceText() const {
  switch (manager_.effectiveChannel()) {
    case update::Channel::Beta: return tr("checks every hour while the Player runs");
    case update::Channel::Stable: return tr("checks every 24 hours while the Player runs");
    default: return {};
  }
}

bool UpdatesController::canInstall() const {
  const auto s = manager_.state();
  return manager_.applySupported() && (s == UpdateManager::State::Available || s == UpdateManager::State::Ready) &&
         !manager_.busy();
}

QString UpdatesController::bannerAction() const {
  const auto s = manager_.state();
  if (s == UpdateManager::State::Ready && manager_.applySupported()) {
    return manager_.autoInstallEffective() ? QStringLiteral("restart") : QStringLiteral("install");
  }
  if (s == UpdateManager::State::Available) {
    if (manager_.applySupported()) return QStringLiteral("install");
    return manager_.notesUrl().isEmpty() ? QStringLiteral("none") : QStringLiteral("link");
  }
  return QStringLiteral("none");
}

QString UpdatesController::bannerActionLabel() const {
  const QString a = bannerAction();
  if (a == QLatin1String("install")) return tr("Install and restart");
  if (a == QLatin1String("restart")) return tr("Restart to update");
  if (a == QLatin1String("link")) return tr("View release");
  return {};
}

QString UpdatesController::bannerText() const {
  const auto s = manager_.state();
  const QString v = manager_.availableVersion();
  if (v.isEmpty() || v == dismissedVersion_) return {};
  switch (s) {
    case UpdateManager::State::Available: return tr("FrameBeam Player %1 is available").arg(v);
    case UpdateManager::State::Ready: return tr("FrameBeam Player %1 is ready to install").arg(v);
    case UpdateManager::State::Downloading: return tr("Downloading FrameBeam Player %1…").arg(v);
    case UpdateManager::State::Incompatible: return tr("FrameBeam Player %1 needs a newer Hub").arg(v);
    default: break;
  }
  return {};
}

void UpdatesController::setChannel(const QString& channel) {
  settings_->setUpdateChannel(channel == QLatin1String("default") ? QString() : channel);
  dismissedVersion_.clear();
  manager_.settingsChanged();
}

void UpdatesController::setAutoInstall(bool on) {
  settings_->setUpdateAutoInstall(on);
  manager_.settingsChanged();
}

void UpdatesController::openNotes() {
  const QUrl u(manager_.notesUrl());
  if (u.isValid() && u.scheme() == QLatin1String("https")) QDesktopServices::openUrl(u);
}

void UpdatesController::dismissBanner() {
  dismissedVersion_ = manager_.availableVersion();
  emit changed();
}

}  // namespace framebeam::ui
