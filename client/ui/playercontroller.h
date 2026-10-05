#pragma once
// PlayerController: verbindet HubConnection, HubLibrary, RomDownloader, Emulation und GameSession mit der
// QML-Oberflaeche. Die QML-Screens lesen nur Properties (screen, hubs, pairing, selectedGame, ...) und
// rufen Aktionen auf; Hub-Logik bleibt in network/ und core/.

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "core_locator.h"
#include "gamesession.h"
#include "hubconnection.h"
#include "hublibrary.h"
#include "librarymodel.h"
#include "romcache.h"
#include "romdownloader.h"
#include "system_manifest.h"

namespace framebeam::ui {

class PlayerController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Wird in main.cpp erzeugt")

  // Navigation: "connection" | "pairing" | "library" | "game"
  Q_PROPERTY(QString screen READ screen NOTIFY screenChanged)

  // 3a
  Q_PROPERTY(QVariantList hubs READ hubs NOTIFY hubsChanged)
  Q_PROPERTY(QString connectionNotice READ connectionNotice NOTIFY hubsChanged)
  Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY autoConnectChanged)
  Q_PROPERTY(QString deviceFooter READ deviceFooter CONSTANT)
  // 3b
  Q_PROPERTY(QVariantMap pairing READ pairing NOTIFY pairingChanged)
  // 3c
  Q_PROPERTY(QString hubName READ hubName NOTIFY hubChanged)
  Q_PROPERTY(QString hubAddress READ hubAddress NOTIFY hubChanged)
  Q_PROPERTY(framebeam::ui::LibraryModel* library READ library CONSTANT)
  Q_PROPERTY(QString libraryState READ libraryState NOTIFY libraryStateChanged)  // loading | ready | error
  Q_PROPERTY(QString libraryError READ libraryError NOTIFY libraryStateChanged)
  Q_PROPERTY(QString selectedGameId READ selectedGameId NOTIFY selectedGameChanged)
  Q_PROPERTY(QVariantMap selectedGame READ selectedGame NOTIFY selectedGameChanged)
  // Spielansicht
  Q_PROPERTY(framebeam::ui::GameSession* gameSession READ gameSession CONSTANT)

 public:
  struct Options {
    QString dataDir;                // leer: ProfileStore::defaultBaseDir()
    bool allowHttp = false;         // --dev-allow-http
    bool memoryCredentials = false; // Tests: kein OS-Credential-Store
    bool probeCoreVersions = true;  // Core-Version fuer den Handshake ermitteln (laedt den Core kurz)
  };

  explicit PlayerController(const Options& options, QObject* parent = nullptr);
  ~PlayerController() override;

  // Auto-Connect zum zuletzt verwendeten Hub, falls aktiviert.
  void startup();

  QString screen() const;
  QVariantList hubs() const;
  QString connectionNotice() const { return notice_; }
  bool autoConnect() const;
  void setAutoConnect(bool on);
  QString deviceFooter() const;
  QVariantMap pairing() const;
  QString hubName() const;
  QString hubAddress() const;
  LibraryModel* library() { return &model_; }
  QString libraryState() const { return libraryState_; }
  QString libraryError() const { return libraryError_; }
  QString selectedGameId() const { return selectedId_; }
  QVariantMap selectedGame() const;
  GameSession* gameSession() { return &session_; }

  // Zugriff fuer Tests.
  HubConnection* connection() { return conn_.get(); }
  HubLibrary* hubLibrary() { return library_.get(); }
  RomDownloader* downloader() { return downloader_.get(); }
  ProfileStore* profileStore() { return profiles_.get(); }
  const QList<CoreInfo>& handshakeCores() const { return coreList_; }

  static QString formatFingerprint(const QString& fp);
  static QString platformLabel(const QString& platform, const QString& arch);

  // 3a / 3b
  Q_INVOKABLE void addHub(const QString& address);
  Q_INVOKABLE void connectProfile(const QString& hubId);
  Q_INVOKABLE void retryConnection();
  Q_INVOKABLE void removeHub(const QString& hubId);  // leer: laufenden Versuch verwerfen
  Q_INVOKABLE void confirmTrust();
  Q_INVOKABLE void rejectTrust();
  Q_INVOKABLE void requestPairing();
  Q_INVOKABLE void cancelPairing();
  Q_INVOKABLE void leavePairing();
  // 3c
  Q_INVOKABLE void switchHub();
  Q_INVOKABLE void reloadLibrary();
  Q_INVOKABLE void selectGame(const QString& gameId);
  Q_INVOKABLE void playSelected();
  // Spielansicht
  Q_INVOKABLE void quitGame();

 signals:
  void screenChanged();
  void hubsChanged();
  void autoConnectChanged();
  void pairingChanged();
  void hubChanged();
  void libraryStateChanged();
  void selectedGameChanged();

 private:
  enum class PlayPhase { None, Rom, Launching };

  void onConnectionState(HubConnection::State s);
  void onLibraryLoaded();
  void onRomStatus(const QString& sha, const RomStatus& st);
  void onRomReady(const QString& sha, const QString& path);
  void launch(const GameEntry& game, const QString& romPath);
  void updateScreen();
  void probeCores();
  const emu::SystemManifest* manifestFor(const GameEntry& game) const;
  QString coreLabel(const emu::SystemManifest& m, const emu::CoreLocation& loc) const;
  QString systemDir() const;
  QString saveDirForCurrentHub() const;
  QVariantMap hubCard(const HubProfile& p) const;

  Options options_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<CredentialStore> credentials_;
  std::unique_ptr<HubConnection> conn_;
  std::unique_ptr<HubLibrary> library_;
  std::unique_ptr<RomCache> cache_;
  std::unique_ptr<RomDownloader> downloader_;
  emu::ManifestRegistry manifests_;
  emu::CoreLocator locator_;
  QList<CoreInfo> coreList_;
  QMap<QString, QString> coreNames_;  // core_id -> Name aus der Core-Info (nur wenn geprobt)
  LibraryModel model_;
  GameSession session_;

  QString screen_ = QStringLiteral("connection");
  bool pairingFlow_ = false;
  QString notice_;
  QString lastError_;
  QString libraryState_ = QStringLiteral("loading");
  QString libraryError_;
  QString selectedId_;
  QString startError_;
  QString pendingSha_;
  PlayPhase phase_ = PlayPhase::None;
  bool gameActive_ = false;
};

}  // namespace framebeam::ui
