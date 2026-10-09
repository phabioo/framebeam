#pragma once
// PlayerController: connects HubConnection, HubLibrary, RomDownloader, emulation and GameSession to the
// QML UI. The QML screens only read properties (screen, hubs, pairing, selectedGame, ...) and
// call actions; hub logic stays in network/ and core/.

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "controllerscontroller.h"
#include "core_locator.h"
#include "emulationcontroller.h"
#include "firmwarecache.h"
#include "corecache.h"
#include "coreprovisioner.h"
#include "firmwareprovisioner.h"
#include "corecatalog.h"
#include "gamedetail.h"
#include "gamesession.h"
#include "gamestarter.h"
#include "gameuploader.h"
#include "hubconnection.h"
#include "hublibrary.h"
#include "hubpresenter.h"
#include "hubsystems.h"
#include "librarymodel.h"
#include "playphase.h"
#include "playersettings.h"
#include "romcache.h"
#include "romdownloader.h"
#include "savehistorycontroller.h"
#include "savesync.h"
#include "sessioncontroller.h"
#include "system_manifest.h"
#include "updatescontroller.h"

namespace framebeam::ui {

class PlayerController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created in main.cpp")

  // Navigation: "connection" | "pairing" | "library" | "game" | "settings"
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
  // Game paused and kept loaded while the Library is shown: {id, title}; empty map when there is none (0.7.x).
  Q_PROPERTY(QVariantMap backgroundGame READ backgroundGame NOTIFY backgroundGameChanged)
  // "Quit game" was asked for and is running ("Saving and syncing…" in the game panel and the Now running strip).
  Q_PROPERTY(bool quitting READ quitting NOTIFY quittingChanged)
  // Confirmation "Quit {running} and start {new}?": {active, runningTitle, newTitle}
  Q_PROPERTY(QVariantMap startConfirm READ startConfirm NOTIFY startConfirmChanged)
  // Save sync (3d): conflict dialog data; empty map = no dialog
  Q_PROPERTY(QVariantMap saveConflict READ saveConflict NOTIFY saveConflictChanged)
  Q_PROPERTY(QString saveNote READ saveNote NOTIFY hubChanged)
  // Game view
  Q_PROPERTY(framebeam::ui::GameSession* gameSession READ gameSession CONSTANT)
  // Appearance (Settings page), upload, core warnings
  Q_PROPERTY(QString appearance READ appearance WRITE setAppearance NOTIFY appearanceChanged)  // dark | light | system
  Q_PROPERTY(bool darkMode READ darkMode NOTIFY appearanceChanged)  // effective palette (System resolved)
  Q_PROPERTY(bool canUpload READ canUpload NOTIFY hubChanged)       // handshake feature uploads_v1
  Q_PROPERTY(QVariantMap upload READ upload NOTIFY uploadChanged)   // active, fileName, progress, message, isError
  Q_PROPERTY(QStringList uploadFilters READ uploadFilters CONSTANT)
  Q_PROPERTY(QVariantList coreWarnings READ coreWarnings NOTIFY hubChanged)  // core_missing / core_version_mismatch
  // Emulation page (3e), Controllers page (3f), applied settings
  Q_PROPERTY(framebeam::ui::EmulationController* emulation READ emulation CONSTANT)
  Q_PROPERTY(framebeam::ui::ControllersController* controllers READ controllers CONSTANT)
  Q_PROPERTY(bool fullscreenOnStart READ fullscreenOnStart NOTIFY emulationSettingsChanged)  // effective FrameBeam option
  // Sessions (3c list, 3g panel, multiview, diagnostics)
  Q_PROPERTY(framebeam::ui::SessionController* sessions READ sessions CONSTANT)
  // Updates (Settings section + banner)
  Q_PROPERTY(framebeam::ui::UpdatesController* updates READ updates CONSTANT)
  // Save slot picker, history/restore, snapshots, "save changed elsewhere" notice (ADR 0012 D7)
  Q_PROPERTY(framebeam::ui::SaveHistoryController* saveHistory READ saveHistory CONSTANT)
  // Log file path (empty when file logging is not active) and "open folder" (Settings)
  Q_PROPERTY(QString logFile READ logFile CONSTANT)
  // Shell (sidebar user block, Settings footer)
  Q_PROPERTY(QString deviceName READ deviceName CONSTANT)
  Q_PROPERTY(QString userName READ userName NOTIFY hubChanged)  // Hub user (handshake `user`); empty for older Hubs
  Q_PROPERTY(QString playerVersion READ playerVersion CONSTANT)
  Q_PROPERTY(QString platformText READ platformText CONSTANT)  // "Windows x86-64 · Protocol v1"

 public:
  struct Options {
    QString dataDir;                // empty: ProfileStore::defaultBaseDir()
    bool allowHttp = false;         // --dev-allow-http
    bool memoryCredentials = false; // tests: no OS credential store
    bool probeCoreVersions = true;  // determine core version for the handshake (briefly loads the core)
    bool enableGamepads = true;     // SDL3 gamepads (tests without hardware use SDL virtual joysticks)
    bool enableUpdates = false;     // schedule update checks (the app enables it; tests never touch the network)
    QString updateIndexUrl;         // tests/CI: instead of env FRAMEBEAM_PLAYER_UPDATE_INDEX_URL / the default
    int gamepadPollMs = 8;          // <= 0: no poll timer (tests call controllers()->gamepads()->poll())
  };

  explicit PlayerController(const Options& options, QObject* parent = nullptr);
  ~PlayerController() override;

  // Auto-connect to the last used hub, if enabled.
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
  QVariantMap backgroundGame() const;
  // Called when a game starts (msecs 0 = now); tests pass fixed times.
  void recordLastPlayed(const QString& gameId, qint64 msecs = 0);
  // Tests and screenshots (GameSession::setPreview): declare the library game that the preview game stands for.
  void adoptPreviewGame(const QString& gameId);
  QVariantMap startConfirm() const;
  bool gameInBackground() const { return background_; }
  bool quitting() const { return quitting_; }
  GameSession* gameSession() { return &session_; }
  SessionController* sessions() { return sessions_.get(); }
  UpdatesController* updates() { return updates_.get(); }
  SaveHistoryController* saveHistory() { return history_.get(); }
  QString logFile() const;
  QString deviceName() const;
  QString userName() const;
  QString playerVersion() const;
  QString platformText() const;
  // A game or a watched Session is running (updates are never applied then).
  bool sessionBusy() const;
  EmulationController* emulation() { return emulation_.get(); }
  ControllersController* controllers() { return controllers_.get(); }
  bool fullscreenOnStart() const;
  QString currentSystemId() const;
  QString appearance() const;
  void setAppearance(const QString& name);
  bool darkMode() const;
  bool canUpload() const;
  QVariantMap upload() const { return upload_; }
  QStringList uploadFilters() const;
  QVariantList coreWarnings() const;
  QVariantMap saveConflict() const { return conflict_; }
  QString saveNote() const { return saves_ ? saves_->note() : QString(); }
  SaveSync* saveSync() { return saves_.get(); }
  // Ends a running game and waits (max. about 10 s) for the final save upload; also called on destruction.
  void shutdown();

  // Access for tests.
  HubConnection* connection() { return conn_.get(); }
  HubLibrary* hubLibrary() { return library_.get(); }
  RomDownloader* downloader() { return downloader_.get(); }
  ProfileStore* profileStore() { return profiles_.get(); }
  const QList<CoreInfo>& handshakeCores() const { return coreList_; }
  PlayerSettings* playerSettings() { return settings_.get(); }
  HubSystems* hubSystems() { return systems_.get(); }
  GameUploader* gameUploader() { return uploader_.get(); }
  FirmwareCache* firmwareCache() { return fwCache_.get(); }

  static QString formatFingerprint(const QString& fp);
  static QString platformLabel(const QString& platform, const QString& arch);

  // 3a / 3b
  Q_INVOKABLE void addHub(const QString& address);
  Q_INVOKABLE void connectProfile(const QString& hubId);
  Q_INVOKABLE void retryConnection();
  Q_INVOKABLE void removeHub(const QString& hubId);  // empty: discard the running attempt
  // Settings > Hubs > Edit (D3): only host and port of a saved Hub change; hubId, pinned fingerprint, credential and
  // Hub user stay. validateHubAddress returns the messages of docs/design/player.md 3p (empty = valid).
  // editHub reconnects when it is the active Hub; a different certificate at the new address then goes through
  // the "certificate changed" flow and is never accepted silently.
  Q_INVOKABLE QStringList validateHubAddress(const QString& host, const QString& port) const;
  Q_INVOKABLE bool editHub(const QString& hubId, const QString& host, const QString& port);
  // Re-evaluates the core / firmware state of all games, the detail pane and the Emulation page (after a core
  // download or whenever the core cache may have changed) without a restart.
  Q_INVOKABLE void refreshCoreState();
  Q_INVOKABLE void confirmTrust();
  Q_INVOKABLE void rejectTrust();
  // Certificate changed (blocked): re-pins exactly `observedFingerprint` (raw value from the hub card) and reconnects;
  // cancel drops the blocked attempt without touching the profile.
  Q_INVOKABLE void trustChangedCertificate(const QString& observedFingerprint);
  Q_INVOKABLE void cancelCertificateChange();
  Q_INVOKABLE void requestPairing();
  Q_INVOKABLE void cancelPairing();
  // Onboarding invite (3b "Redeem invite"): code + display name.
  Q_INVOKABLE void redeemInvite(const QString& code, const QString& displayName);
  Q_INVOKABLE void leavePairing();
  // 3c
  Q_INVOKABLE void switchHub();
  Q_INVOKABLE void switchToHub(const QString& hubId);  // Settings -> Hubs: switchHub() + connectProfile()
  Q_INVOKABLE void reloadLibrary();
  Q_INVOKABLE void selectGame(const QString& gameId);
  Q_INVOKABLE void playSelected();
  // "Play and share Session": starts the game and publishes it with the last chosen visibility.
  Q_INVOKABLE void playAndShareSelected();
  // "← Library" in the game view: pauses the running game and keeps it loaded in the background (the Session stays
  // shared), or leaves the watched Session. Quitting is quitGame().
  Q_INVOKABLE void leaveGameView();
  // Library "Resume": back to the game view; the game continues (resumed, not left paused).
  Q_INVOKABLE void resumeGame();
  // Starting another game while one is in the background asks first; confirm = quitGame() + start.
  Q_INVOKABLE void confirmQuitAndStart();
  Q_INVOKABLE void cancelQuitAndStart();
  // Library header / Sidebar
  Q_INVOKABLE void showLibrary();
  Q_INVOKABLE void showSettings();
  Q_INVOKABLE void openLogFolder();
  Q_INVOKABLE void showEmulation();
  Q_INVOKABLE void showControllers();
  // "Upload ROM": source = local path or file:// URL (from the file dialog). Streamed; progress in upload.
  Q_INVOKABLE void uploadRom(const QString& source);
  Q_INVOKABLE void dismissUploadMessage();
  // Detail pane "Check again": reloads the systems registry (firmware status) after the admin changed something.
  Q_INVOKABLE void recheckFirmware();
  // Game view
  Q_INVOKABLE void quitGame();
  // "Quit game" of the panel and the sidebar strip: shows the "Saving and syncing…" state for a moment, then quitGame().
  Q_INVOKABLE void requestQuit();
  // Conflict dialog: "use_hub" | "use_local" | "later" (Keep both, decide later)
  Q_INVOKABLE void resolveSaveConflict(const QString& action);

 signals:
  void screenChanged();
  void hubsChanged();
  void autoConnectChanged();
  void pairingChanged();
  void hubChanged();
  void libraryStateChanged();
  void selectedGameChanged();
  void backgroundGameChanged();
  void quittingChanged();
  void startConfirmChanged();
  void saveConflictChanged();
  void appearanceChanged();
  void uploadChanged();
  void emulationSettingsChanged();

 private:
  void onConnectionState(HubConnection::State s);
  void onLibraryLoaded();
  void onRomStatus(const QString& sha, const RomStatus& st);
  void updateScreen();
  void refreshEmulationPage();
  void refreshAttention();
  void applyFrameBeamOptions();
  void onUploadFinished(const UploadResult& result);
  QString friendlyError(const QString& code, const QString& message) const;
  void endRunningWork();

  Options options_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<CredentialStore> credentials_;
  std::unique_ptr<HubConnection> conn_;
  std::unique_ptr<HubLibrary> library_;
  std::unique_ptr<RomCache> cache_;
  std::unique_ptr<RomDownloader> downloader_;
  std::unique_ptr<SaveSync> saves_;
  std::unique_ptr<PlayerSettings> settings_;
  std::unique_ptr<FirmwareCache> fwCache_;
  std::unique_ptr<HubSystems> systems_;
  std::unique_ptr<FirmwareProvisioner> provisioner_;
  std::unique_ptr<CoreCache> coreCache_;
  std::unique_ptr<CoreProvisioner> coreProv_;
  QMap<QString, QString> coreProblems_;  // core_id -> last provisioning problem reason (cleared with a fresh registry)
  HandshakeInfo handshake_;
  std::unique_ptr<GameUploader> uploader_;
  emu::ManifestRegistry manifests_;
  emu::CoreLocator locator_;
  QList<CoreInfo> coreList_;
  QMap<QString, QString> coreVersions_;  // core_id -> version from the core info
  QMap<QString, QString> coreNames_;  // core_id -> name from the core info (only if probed)
  LibraryModel model_;
  GameSession session_;
  std::unique_ptr<SessionController> sessions_;
  std::unique_ptr<UpdatesController> updates_;
  std::unique_ptr<SaveHistoryController> history_;  // after saves_/conn_: destroyed first
  std::unique_ptr<EmulationController> emulation_;
  std::unique_ptr<ControllersController> controllers_;
  // Helpers (after everything they reference): they read/write the state below through references.
  std::unique_ptr<CoreCatalog> catalog_;
  std::unique_ptr<GameDetail> detail_;
  std::unique_ptr<HubPresenter> hubPresenter_;
  std::unique_ptr<GameStarter> starter_;
  bool shareOnStart_ = false;
  QTimer* liveTimer_ = nullptr;     // periodic quiet refresh of the Library and the core state while it is shown
  bool quietRefresh_ = false;       // the running library reload is the periodic one (no "loading" state, errors ignored)
  QString resumePage_;              // page to return to after the reconnect of an edited active Hub
  void endGameContext();

  QString screen_ = QStringLiteral("connection");
  QString page_ = QStringLiteral("library");  // page shown while connected: library | settings | emulation | controllers
  bool inviteBusy_ = false;
  QVariantMap upload_;
  QString pendingSelectId_;             // select this game once the library reloaded (after an upload)
  QList<FirmwareProblem> fwProblems_;   // last validation problems (cleared by recheckFirmware/new registry)
  QMap<QString, QString> fwOptions_;    // firmware core options of the launch in progress
  bool pairingFlow_ = false;
  bool lastAttemptPairing_ = false;  // last attempt came from addHub (true) or connectProfile (false)
  QString notice_;
  QString lastError_;
  QString libraryState_ = QStringLiteral("loading");
  QString libraryError_;
  QString selectedId_;
  QString startError_;
  QString pendingSha_;
  PlayPhase phase_ = PlayPhase::None;
  bool gameActive_ = false;
  bool background_ = false;      // gameActive_ and paused behind the Library (input blocked)
  QString backgroundId_;
  QString backgroundTitle_;
  qint64 gamePausedMs_ = 0;      // epoch ms when the game went to the background (3c-5 detail column: "paused 22:31")
  qint64 gameStartedMs_ = 0;     // epoch ms of the start of the running game ("12 min" in the Now running strip)
  bool quitting_ = false;
  bool confirmActive_ = false;   // "Quit X and start Y?" is open
  bool confirmShare_ = false;
  void setBackground(bool on);
  void askQuitAndStart(bool share);
  // Start of a game: after the ROM and the save sync
  GameEntry launchGame_;
  QString launchRom_;
  bool saveReady_ = false;
  QString saveNoteStart_;  // note of the last start sync (e.g. Hub not reachable)
  QVariantMap conflict_;
};

}  // namespace framebeam::ui
