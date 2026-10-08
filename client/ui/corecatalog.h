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

#include "core_locator.h"
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
  // Core lookup with the version the Hub serves (cache source), see CoreLocator.
  emu::CoreLocation locateCore(const emu::SystemManifest& man) const;
  // The Hub offers a core package for this system (cores_v1 + core_package_version of the preferred core).
  bool hubOffersCore(const emu::SystemManifest& man, QString* version = nullptr) const;
  // Located core is usable as is (a cached core must also pass the SHA-256 check).
  bool coreUsable(const emu::SystemManifest& man, emu::CoreLocation* loc = nullptr) const;
  // Core status text/tone/hint for the detail pane and the system cards.
  void coreStatus(const emu::SystemManifest& man, QString* text, QString* tone, QString* hint) const;
  static QString coreProblemText(const QString& reason);
  QString attentionFor(const GameEntry& game) const;
  QVariantList systemCards();
  QString coreLabel(const emu::SystemManifest& m, const emu::CoreLocation& loc) const;
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
};

}  // namespace framebeam::ui
