#pragma once
// CoreCatalog: system manifests, core lookup/probing and the core / firmware status of a system (Library "needs
// attention", detail pane, Emulation page cards). Read-mostly helper of PlayerController; the shared state
// (manifests, probe results, provisioning problems) stays owned by PlayerController and is referenced here.

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <map>

#include "core_locator.h"
#include "corechoice.h"
#include "corecache.h"
#include "coreprovisioner.h"
#include "emulationcontroller.h"
#include "firmwarecache.h"
#include "firmwareprovisioner.h"
#include "gamesession.h"
#include "hubconnection.h"
#include "hublibrary.h"
#include "hubsystems.h"
#include "librarymodel.h"
#include "playphase.h"
#include "romcache.h"
#include "romdownloader.h"
#include "savesync.h"
#include "system_manifest.h"

namespace framebeam::ui {

class CoreCatalog : public QObject {
  Q_OBJECT

 public:
  struct Deps {
    HubConnection* conn;
    ProfileStore* profiles;
    HubSystems* systems;
    CoreCache* coreCache;
    EmulationController* emulation;
    emu::ManifestRegistry& manifests;
    emu::CoreLocator& locator;
    QList<CoreInfo>& coreList;
    QMap<QString, QString>& coreVersions;  // core_id -> version from the core info
    QMap<QString, QString>& coreNames;     // core_id -> name from the core info (only if probed)
    QMap<QString, QString>& coreProblems;  // core_id -> last provisioning problem reason
    const QList<FirmwareProblem>& fwProblems;
    const PlayPhase& phase;
    const GameEntry& launchGame;
  };

  explicit CoreCatalog(const Deps& d, QObject* parent = nullptr);

  // `<base>/system` (cores' system directory and firmware cache); usable before a CoreCatalog exists.
  static QString systemDirIn(const QString& baseDir);

  void probeCores();
  // Effective core of a system (and game): game > system > Hub default, among the cores the Hub serves (ADR 0020 D6).
  // Without Hub data (offline): the stored choice, else the first profiled core that is available locally.
  CoreChoice choiceFor(const QString& systemId, const QString& gameId = QString()) const;
  // Effective manifest (system + core profile; experimental without profile) of a system / game. Pointers stay valid.
  const emu::SystemManifest* manifestForSystem(const QString& systemId, const QString& gameId = QString()) const;
  // Cores of a system for the Emulation page: [{id, name, version, license, buildDate, experimental, requiredHwApi,
  // isDefault, label}]; the Hub's list, else the local profiles.
  QVariantList coresOf(const QString& systemId) const;
  QString reportedCoreVersion(const QString& coreId) const;  // version from the core info (probe); empty = unknown
  QString systemDefaultCore(const QString& systemId) const;  // Hub default core id, else the first profile
  // "A stored choice is not served any more" notice for the effective core of this system / game; empty = none.
  QString coreNoticeFor(const QString& systemId, const QString& gameId = QString()) const;
  // Core lookup with the version the Hub serves (cache source), see CoreLocator.
  emu::CoreLocation locateCore(const emu::SystemManifest& man) const;
  // The Hub serves this core for the system (cores_v1: SystemCore version, or the legacy preferred core).
  bool hubOffersCore(const emu::SystemManifest& man, QString* version = nullptr) const;
  // Located core is usable as is (a cached core must also pass the SHA-256 check).
  bool coreUsable(const emu::SystemManifest& man, emu::CoreLocation* loc = nullptr) const;
  // Core status text/tone/hint for the detail pane and the system cards.
  void coreStatus(const emu::SystemManifest& man, QString* text, QString* tone, QString* hint) const;
  static QString coreProblemText(const QString& reason);
  QString attentionFor(const GameEntry& game) const;
  QVariantList systemCards();
  // Core name for the UI; cores without profile carry the "Experimental" marker.
  QString coreLabel(const emu::SystemManifest& m, const emu::CoreLocation& loc) const;
  // Effective manifest of the game's system for the game's effective core.
  const emu::SystemManifest* manifestFor(const GameEntry& game) const;
  QStringList wantedFirmwareIds(const emu::SystemManifest& man) const;
  // Firmware mode of the Hub for the system of this manifest: true = native (files required).
  bool nativeFirmware(const emu::SystemManifest& man, SystemInfo* system = nullptr) const;
  QString systemDir() const;

 private:
  HubConnection* conn_;
  ProfileStore* profiles_;
  HubSystems* systems_;
  CoreCache* coreCache_;
  EmulationController* emulation_;
  emu::ManifestRegistry& manifests_;
  emu::CoreLocator& locator_;
  QList<CoreInfo>& coreList_;
  QMap<QString, QString>& coreVersions_;
  QMap<QString, QString>& coreNames_;
  QMap<QString, QString>& coreProblems_;
  const QList<FirmwareProblem>& fwProblems_;
  const PlayPhase& phase_;
  const GameEntry& launchGame_;
  mutable std::map<QString, emu::SystemManifest> effective_;
};

}  // namespace framebeam::ui
