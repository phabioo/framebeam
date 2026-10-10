#pragma once
// Player updater (spec 0.3 S6): check the signed update index, download + verify + stage the installer,
// apply it (Windows installer install only). Never applies during a running game.

#include <QByteArray>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

#include "installroot.h"
#include "playersettings.h"
#include "updateindex.h"

class QNetworkReply;

namespace framebeam::update {

// Index fetch result (also used by the CLI): raw bytes verified against the trusted keys and parsed.
struct FetchedIndex {
  bool ok = false;
  QString error;
  QByteArray indexBytes;
  QByteArray sigBytes;
  Index index;
};

// A staged installer that passed the full verification (signature of the stored index, release entry, size,
// SHA-256, version strictly newer than the running one).
struct StagedUpdate {
  QString version;
  QString installerPath;
  QString notesUrl;
};
// Looks into <baseDir>/cache/updates/<version>/ (index.json, index.json.sig, staged.json, installer) and returns
// the highest verified staged update; stale or invalid staging directories are removed.
std::optional<StagedUpdate> loadVerifiedStaged(const QString& baseDir, const QString& currentVersion,
                                               const QList<QByteArray>& trustedKeys, bool allowFileUrls);
QString stagingRoot(const QString& baseDir);  // <baseDir>/cache/updates

// Verifies size and SHA-256 of a file against an artifact.
bool verifyArtifactFile(const QString& path, const Artifact& artifact, QString* error = nullptr);

class UpdateManager : public QObject {
  Q_OBJECT
 public:
  enum class State {
    Idle,         // nothing checked yet
    Disabled,     // channel off (dev build without explicit channel)
    Checking,
    UpToDate,
    Available,    // newer release found; not downloaded
    Incompatible, // newer release needs a newer Hub
    Downloading,
    Ready,        // verified installer staged
    Applying,
    Error,
  };
  Q_ENUM(State)

  struct Config {
    QString baseDir;          // data directory
    QString indexUrl;         // empty: env FRAMEBEAM_PLAYER_UPDATE_INDEX_URL, else the default URL
    QString currentVersion;
    QString compiledChannel;  // stable | beta | dev
    QList<QByteArray> trustedKeys;
    QString installRoot;      // install root (installRootFor), empty = unknown
    bool forceApplySupport = false;  // tests: behave like an installed Windows Player
  };

  UpdateManager(const Config& config, PlayerSettings* settings, QObject* parent = nullptr);

  // Providers (optional): Hub protocol of the current Hub (S4), "a game/Session is running" (blocks apply).
  void setHubProtocolProvider(std::function<std::optional<HubProtocol>()> f) { hubProtocol_ = std::move(f); }
  void setBusyProvider(std::function<bool()> f) { busy_ = std::move(f); }
  // Starts the installer (default: QProcess::startDetached). Tests inject a fake.
  void setLauncher(std::function<bool(const QString& program, const QStringList& args)> f) { launcher_ = std::move(f); }

  Channel effectiveChannel() const;
  // Effective channel: explicit setting > resolved default (beta builds only) > compiled channel.
  bool channelIsOverridden() const { return !settings_->updateChannel().isEmpty(); }
  // Automatic install is only effective on the beta channel (stable always asks first).
  bool autoInstallSetting() const;
  bool autoInstallEffective() const { return effectiveChannel() == Channel::Beta && autoInstallSetting(); }
  // Only an installer-based Windows install updates itself; zip/dev/Linux just show the release.
  bool applySupported() const;
  QString applyUnsupportedReason() const;

  State state() const { return state_; }
  QString statusText() const { return status_; }
  QString availableVersion() const { return selection_.release.version; }
  QString notesUrl() const { return selection_.release.notesUrl; }
  double progress() const { return progress_; }
  QDateTime lastCheck() const { return lastCheck_; }
  const QString& currentVersion() const { return config_.currentVersion; }
  QString indexUrl() const { return indexUrl_; }
  bool busy() const { return busy_ && busy_(); }

  // Timers: first check 10 s after start, then every 1 h (beta) / 24 h (stable).
  void start(int firstDelayMs = 10000);
  void checkNow();
  // "Install and restart" / "Restart to update": downloads when needed, then applies. Refused during a game.
  void installNow();
  // Channel or automatic-install setting changed: re-evaluate and check right away.
  void settingsChanged();
  QString lastError() const { return lastError_; }
  // Beta channel + automatic: a verified staged installer from the last run is applied right now. Returns true
  // when the installer was started and the Player has to quit (never during a game). Used before the main window.
  bool applyStagedAtStart();

  // Fetches and verifies an index (used by check and by the CLI). indexUrl may be https:// or file://.
  static void fetchIndex(QNetworkAccessManager* nam, const QUrl& indexUrl, const QList<QByteArray>& trustedKeys,
                         QObject* context, std::function<void(const FetchedIndex&)> done);

 signals:
  void changed();
  void quitRequested();  // installer started: the Player has to exit now

 private:
  void resolveDefaultChannel(const Index& index);
  void setState(State s, const QString& text);
  void onIndex(const FetchedIndex& fi);
  void startDownload(bool applyAfter);
  void finishDownload(const QString& partPath);
  void apply(const StagedUpdate& staged);
  bool hasAttemptRecord(const StagedUpdate& staged) const;
  void recordAttempt(const StagedUpdate& staged);
  bool allowFileUrls() const;
  void scheduleNext();

  Config config_;
  PlayerSettings* settings_;
  QString indexUrl_;
  QNetworkAccessManager nam_;
  QTimer timer_;
  State state_ = State::Idle;
  QString status_;
  Selection selection_;
  FetchedIndex lastIndex_;
  double progress_ = 0;
  QDateTime lastCheck_;
  QString lastError_;
  bool checkRunning_ = false;
  bool applyAfterDownload_ = false;
  QPointer<QNetworkReply> download_;
  std::function<std::optional<HubProtocol>()> hubProtocol_;
  std::function<bool()> busy_;
  std::function<bool(const QString&, const QStringList&)> launcher_;
};

}  // namespace framebeam::update
