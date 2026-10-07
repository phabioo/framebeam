#pragma once
// SaveHistoryController: save slot picker, save history with restore, manual snapshots and the "save changed on
// another device" notice for the QML UI (ADR 0012 D7). Slot choice, restore, snapshots and the sync logic live in
// network/ (SaveSync, SaveApi); this class only holds the view state of the selected game.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>

#include "hubconnection.h"
#include "savesync.h"

namespace framebeam::ui {

class SaveHistoryController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")

  // Hub advertises saves_v1 (slot picker) / saves_v2 (history, restore, snapshots).
  Q_PROPERTY(bool slotsAvailable READ slotsAvailable NOTIFY changed)
  Q_PROPERTY(bool available READ available NOTIFY changed)
  Q_PROPERTY(QString gameId READ gameId NOTIFY changed)
  Q_PROPERTY(QString slot READ slot NOTIFY changed)
  Q_PROPERTY(QVariantList slotOptions READ slotOptions NOTIFY changed)  // [{value, label}]
  // [{version, versionText, revision, label, reason, reasonText, device, when}] newest first
  Q_PROPERTY(QVariantList history READ history NOTIFY changed)
  Q_PROPERTY(bool loading READ loading NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  // The selected game runs (or is starting) on this Player: no slot change, no restore.
  Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY changed)
  // Why restoring is blocked right now (empty = possible).
  Q_PROPERTY(QString restoreBlockReason READ restoreBlockReason NOTIFY changed)
  Q_PROPERTY(bool canSnapshot READ canSnapshot NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)
  Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY changed)
  // Pending restore confirmation: {} = none, else {version, versionText, label, reasonText, device, when, slot, gameTitle}.
  Q_PROPERTY(QVariantMap restoreRequest READ restoreRequest NOTIFY changed)
  // In game: "the save changed on <device>" (empty when none or no game runs).
  Q_PROPERTY(QString notice READ notice NOTIFY changed)

 public:
  struct Env {
    SaveSync* saves = nullptr;
    HubConnection* connection = nullptr;
    std::function<bool(const QString&)> gameBusy;         // game is starting/running on this Player
    std::function<QString(const QString&)> localFileName;  // expected local save file name of a game (may be empty)
  };
  explicit SaveHistoryController(const Env& env, QObject* parent = nullptr);

  bool slotsAvailable() const;
  bool available() const;
  QString gameId() const { return gameId_; }
  QString slot() const;
  QVariantList slotOptions() const;
  QVariantList history() const { return history_; }
  bool loading() const { return pending_ > 0; }
  bool busy() const { return busy_; }
  bool gameRunning() const;
  QString restoreBlockReason() const;
  bool canSnapshot() const;
  QString message() const { return message_; }
  bool messageIsError() const { return messageIsError_; }
  QString notice() const;
  QVariantMap restoreRequest() const { return restoreRequest_; }

  // The selected game of the library changed (empty: none).
  void setGame(const QString& gameId);
  // The game phase (starting/running/ended) may have changed: update the restore block and clear stale messages.
  void onGameStateChanged();
  // save_updated from the Hub (see SaveSync::saveChangedElsewhere).
  void onSaveUpdated(const SaveUpdate& update, bool runningHere);

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void selectSlot(const QString& slot);
  // "New slot": empty string = created and selected, otherwise the validation error.
  Q_INVOKABLE QString createSlot(const QString& name);
  Q_INVOKABLE QString validateSlotName(const QString& name) const;  // empty = valid
  // Restore with confirmation: requestRestore() opens the dialog data, confirmRestore() runs it, cancelRestore() closes it.
  Q_INVOKABLE void requestRestore(int version);
  Q_INVOKABLE void confirmRestore();
  Q_INVOKABLE void cancelRestore();
  Q_INVOKABLE void restore(int version);
  Q_INVOKABLE void createSnapshot(const QString& label);        // detail pane
  Q_INVOKABLE void createSnapshotInGame(const QString& label);  // running game: uploads a changed save first
  Q_INVOKABLE void dismissMessage();
  Q_INVOKABLE void dismissNotice();

 signals:
  void changed();

 private:
  void setMessage(const QString& text, bool isError);
  void recomputeBlock();
  static QString reasonText(const QString& reason);
  static QString formatWhen(const QString& iso);

  Env env_;
  QString gameId_;
  QVariantList history_;
  QStringList remoteSlots_;  // slots of this game on the Hub
  int currentRevision_ = 0;
  int pending_ = 0;
  quint64 gen_ = 0;
  bool busy_ = false;
  QString message_;
  bool messageIsError_ = false;
  QString notice_;
  QString blockReason_;
  QVariantMap restoreRequest_;
  bool lastBusy_ = false;
};

}  // namespace framebeam::ui
