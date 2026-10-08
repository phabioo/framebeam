#pragma once
// GameDetail: the data map of the detail pane for the selected game (ROM / core / firmware / save rows, start
// checklist, Play button state). Read-only view of PlayerController state, which is referenced here.

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
#include "corecatalog.h"

namespace framebeam::ui {

class GameDetail : public QObject {
  Q_OBJECT

 public:
  struct Deps {
    LibraryModel& model;
    const QString& selectedId;
    SaveSync* saves;
    RomCache* cache;
    HubConnection* conn;
    HubSystems* systems;
    FirmwareCache* fwCache;
    CoreCatalog* catalog;
    const QMap<QString, QString>& coreVersions;
    const QList<FirmwareProblem>& fwProblems;
    const PlayPhase& phase;
    const bool& saveReady;
    const QString& saveNoteStart;
    const QString& startError;
  };

  explicit GameDetail(const Deps& d, QObject* parent = nullptr);

  QVariantMap selectedGame() const;

 private:
  LibraryModel& model_;
  const QString& selectedId_;
  SaveSync* saves_;
  RomCache* cache_;
  HubConnection* conn_;
  HubSystems* systems_;
  FirmwareCache* fwCache_;
  CoreCatalog* catalog_;
  const QMap<QString, QString>& coreVersions_;
  const QList<FirmwareProblem>& fwProblems_;
  const PlayPhase& phase_;
  const bool& saveReady_;
  const QString& saveNoteStart_;
  const QString& startError_;
};

}  // namespace framebeam::ui
