#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <optional>

#include "hubconnection.h"

namespace framebeam {

// Hub save API (protocol/openapi/framebeam.yaml, tag `saves`, handshake feature `saves_v1`).
inline constexpr const char* kSavesFeature = "saves_v1";
inline constexpr const char* kSavesV2Feature = "saves_v2";  // restore, snapshots, history labels, save_updated (0.4)
inline constexpr const char* kSavesV4Feature = "saves_v4";  // upload of a local save file as the current checkpoint
inline constexpr qint64 kMaxSaveBytes = 64LL * 1024 * 1024;

struct SaveCheckpoint {
  int revision = 0;
  QString sha256;
  qint64 size = 0;
  QString deviceId;
  QString deviceName;
  QString createdAt;  // ISO 8601
  QString reason;     // checkpoint | final | final_session_end | restore | upload
};

// Permanent history version (GET .../history, POST .../snapshots).
struct SaveHistoryVersion {
  int version = 0;
  int revision = 0;
  QString sha256;
  qint64 size = 0;
  QString deviceId;
  QString deviceName;
  QString createdAt;  // ISO 8601
  QString reason;     // session_end | device_change | before_conflict_resolution | conflict_upload | manual_snapshot | before_restore | before_upload
  QString label;      // optional (saves_v2); empty = none
};

// WSS `save_updated` (feature saves_v2): the checkpoint of a slot changed on another device. `reason` is not validated
// (the Hub may add values).
struct SaveUpdate {
  QString gameId;
  QString slot;
  int revision = 0;
  QString sha256;
  QString deviceId;
  QString deviceName;
  QString reason;
};
std::optional<SaveUpdate> parseSaveUpdate(const QJsonObject& payload);

struct SaveConflictInfo {
  QString id;
  QString gameId;
  QString slot;
  QString status;  // open | resolved_hub | resolved_local
  // Hub side
  int hubRevision = 0;
  QString hubSha256;
  QString hubDeviceId;
  QString hubDeviceName;
  QString hubCreatedAt;
  // Secured upload (history version)
  int securedVersion = 0;
  QString securedSha256;
  int securedBaseRevision = 0;
  QString securedDeviceId;
  QString securedDeviceName;
  QString securedCreatedAt;
  QString createdAt;
  bool isOpen() const { return status == QLatin1String("open"); }
};

struct SaveSlotInfo {
  QString gameId;
  QString slot;
  SaveCheckpoint current;
  QList<SaveConflictInfo> openConflicts;
  int openConflictCount = 0;  // from the slot list
};

std::optional<SaveCheckpoint> parseSaveCheckpoint(const QJsonObject& o);
std::optional<SaveConflictInfo> parseSaveConflict(const QJsonObject& o);
std::optional<SaveSlotInfo> parseSaveSlot(const QJsonObject& o);
std::optional<SaveHistoryVersion> parseSaveHistoryVersion(const QJsonObject& o);

struct SaveApiResult {
  enum class Kind {
    Ok,
    NotFound,    // 404
    Conflict,    // 409 save_conflict (upload secured, `conflict` set)
    Stale,       // 409 save_conflict_stale
    Offline,     // no connection, 5xx, 429, token problem: retry later
    Rejected,    // other 4xx (400, 403, 413 ...): retrying does not help
    BadResponse  // malformed or verification failed
  };
  Kind kind = Kind::Offline;
  int status = 0;
  QString errorCode;
  QString errorMessage;
  std::optional<SaveSlotInfo> slot;
  std::optional<SaveConflictInfo> conflict;
  QList<SaveSlotInfo> slotList;  // listSlots
  QList<SaveHistoryVersion> history;  // listHistory (newest first)
  std::optional<SaveHistoryVersion> snapshot;  // createSnapshot
  QByteArray content;         // downloadContent (verified against the ETag)
  int contentRevision = 0;
  bool ok() const { return kind == Kind::Ok; }
};

// Thin async client over the authenticated HubConnection. The callback runs on the event loop (never
// synchronously); it is dropped if the SaveApi is destroyed.
class SaveApi : public QObject {
  Q_OBJECT
 public:
  using Callback = std::function<void(const SaveApiResult&)>;
  explicit SaveApi(HubConnection* connection, QObject* parent = nullptr);

  void listSlots(Callback cb);
  void getSlot(const QString& gameId, const QString& slot, Callback cb);
  void putSave(const QString& gameId, const QString& slot, const QByteArray& data, int baseRevision, const QString& reason,
               Callback cb);
  void getContent(const QString& gameId, const QString& slot, Callback cb);
  void resolve(const QString& gameId, const QString& slot, const QString& conflictId, const QString& resolution,
               int expectedRevision, Callback cb);
  // saves_v2
  void listHistory(const QString& gameId, const QString& slot, Callback cb);
  void restore(const QString& gameId, const QString& slot, int version, int expectedRevision, Callback cb);
  void createSnapshot(const QString& gameId, const QString& slot, const QString& label, Callback cb);
  // saves_v4: POST .../upload; `expectedRevision` 0 = the slot does not exist on the Hub yet. Answer: the new SaveSlot.
  void uploadFile(const QString& gameId, const QString& slot, const QByteArray& data, int expectedRevision, Callback cb);

 private:
  enum class Expect { Slot, SlotList, Content, Put, Resolve, History, Snapshot };
  void run(QNetworkReply* reply, Expect expect, Callback cb);
  void immediate(SaveApiResult::Kind kind, const QString& code, Callback cb);
  static QString slotPath(const QString& gameId, const QString& slot);

  QPointer<HubConnection> conn_;
};

}  // namespace framebeam

Q_DECLARE_METATYPE(framebeam::SaveUpdate)
