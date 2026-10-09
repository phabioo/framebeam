#pragma once
// "Set up a Hub on this PC" without a local Hub (0.9): downloads the MSI of the Player's own version from the signed
// update index, verifies size and SHA-256 like the updater, then hands over to a temporary copy of the launcher that
// runs msiexec elevated (ALLUSERS=1 INSTALL_HUB=1 NETWORK_SHARING=0) and starts the per-machine Player with
// `--setup-local-hub`.

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <optional>

#include "updater.h"

class QNetworkReply;

namespace framebeam::update {

// The MSI artifact of exactly `version` (any channel) for the Player platform; nullopt when the index has none.
std::optional<Artifact> findOwnMsi(const Index& index, const QString& version, Release* releaseOut = nullptr);

class HubSetupInstaller : public QObject {
  Q_OBJECT
 public:
  enum class State { Idle, Fetching, Downloading, Launching, Failed };
  struct Config {
    QString indexUrl;  // empty: env FRAMEBEAM_PLAYER_UPDATE_INDEX_URL, else the default URL
    QString currentVersion;
    QList<QByteArray> trustedKeys;  // empty: trustedKeysFromEnvironment()
    QString installRoot;            // of this Player (its launcher is copied)
    QString downloadDir;            // empty: <temp>/framebeam-hub-setup-<uuid>
    QString programFilesDir;        // empty: env ProgramFiles
  };
  explicit HubSetupInstaller(const Config& config, QObject* parent = nullptr);

  // Tests inject a fake instead of QProcess::startDetached.
  void setLauncher(std::function<bool(const QString&, const QStringList&)> f) { launcher_ = std::move(f); }

  State state() const { return state_; }
  QString error() const { return error_; }
  double progress() const { return progress_; }

  void start();
  void cancel();

 signals:
  void changed();
  void quitRequested();  // launcher started: the Player must exit so the MSI can replace its files

 private:
  void fail(const QString& text);
  void onIndex(const FetchedIndex& fi);
  void download(const Artifact& art);
  void launch(const QString& msiPath);

  Config config_;
  QNetworkAccessManager nam_;
  State state_ = State::Idle;
  QString error_;
  double progress_ = 0;
  QPointer<QNetworkReply> reply_;
  quint64 gen_ = 0;
  std::function<bool(const QString&, const QStringList&)> launcher_;
};

}  // namespace framebeam::update
