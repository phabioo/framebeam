#pragma once
// "Set up a server on this PC" without a local Hub (0.9): downloads the MSI of the Player's own version from the signed
// update index, verifies size and SHA-256 like the updater, then hands over to a temporary copy of the launcher that
// runs msiexec elevated (ALLUSERS=1 INSTALL_HUB=1 NETWORK_SHARING=0) and starts the per-machine Player with
// `--setup-local-hub`.

#include <QDir>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

#include "updater.h"

class QNetworkReply;

namespace uzume::update {

// The MSI artifact of exactly `version` (any channel) for the Player platform; nullopt when the index has none.
std::optional<Artifact> findOwnMsi(const Index& index, const QString& version, Release* releaseOut = nullptr);

class HubSetupInstaller : public QObject {
  Q_OBJECT
 public:
  enum class State { Idle, Fetching, Downloading, Launching, Failed };
  struct Config {
    QString indexUrl;  // empty: env UZUME_APP_UPDATE_INDEX_URL, else the default URL
    QString currentVersion;
    QList<QByteArray> trustedKeys;  // empty: trustedKeysFromEnvironment()
    QString installRoot;            // of this Player (its launcher is copied)
    QString downloadDir;            // empty: <temp>/uzume-server-setup-<uuid>
    QString programFilesDir;        // empty: env ProgramFiles
    // "Share with others" (0.18 package 10): MSI scope ShareHub with these properties (NAME=VALUE) instead of SetupHub.
    QStringList shareProperties;
  };
  explicit HubSetupInstaller(const Config& config, QObject* parent = nullptr);

  // "Share with others": the properties of the next start() (empty = the plain "Set up a server" install).
  void setShareProperties(const QStringList& props) { config_.shareProperties = props; }
  // Tests inject a fake instead of QProcess::startDetached.
  void setLauncher(std::function<bool(const QString&, const QStringList&)> f) { launcher_ = std::move(f); }

  State state() const { return state_; }
  QString error() const { return error_; }
  double progress() const { return progress_; }

  void start();
  void cancel();
  // Removes <temp>/uzume-server-setup-* folders older than a day (a launched setup leaves its folder to the launcher
  // copy, which removes it after msiexec; this catches what a crash or a previous run left behind). Returns the count.
  static int cleanStaleTempDirs(const QString& tempBase = QDir::tempPath(), qint64 maxAgeSecs = 24 * 3600);

 signals:
  void changed();
  void quitRequested();  // launcher started: the Player must exit so the MSI can replace its files

 private:
  void fail(const QString& text);
  void removeTempDir();  // the download folder this installer created itself (never a configured downloadDir)
  void onIndex(const FetchedIndex& fi);
  void download(const Artifact& art);
  void launch(const QString& msiPath, const Artifact& art);

  Config config_;
  QString tempDir_;  // <temp>/uzume-server-setup-<uuid> while we own it
  QNetworkAccessManager nam_;
  State state_ = State::Idle;
  QString error_;
  double progress_ = 0;
  QPointer<QNetworkReply> reply_;
  quint64 gen_ = 0;
  std::function<bool(const QString&, const QStringList&)> launcher_;
};

}  // namespace uzume::update
