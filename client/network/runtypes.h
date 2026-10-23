#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <optional>

namespace uzume {

// Runs (Hub feature runs_v1, 0.11 package 3 contract section 5; schemas protocol/schemas/ws-run*.schema.json).

struct RunPluginRef {
  QString pluginId;
  QString version;
  QString sha256;  // lowercase hex of the .fbplugin the Hub offered when the run was created
};

// One plugin's state object of one participant, with its own revision (addendum: per-plugin run state).
struct RunPluginState {
  QJsonObject state;
  qint64 rev = 0;
  QString updatedAt;
};

struct RunParticipant {
  QString userId;
  QString userName;
  QString status;  // invited | joined | declined | left
  QString gameId;  // Hub library game, empty until joined
  int color = -1;  // 0..7 in join order, -1 until the first join
  QMap<QString, RunPluginState> states;  // by plugin id; a missing plugin means state {} at revision 0
  RunPluginState stateOf(const QString& pluginId) const { return states.value(pluginId); }
};

struct RunInfo {
  QString runId;
  QString name;
  QString status;  // starting | active | paused | finished | abandoned
  QString createdBy;
  QString createdAt;
  QString updatedAt;
  QString statusBy;
  QString saveSlot;  // Hub field save_slot ("run-" + 8 hex); runSaveSlot(runId) when the Hub does not send it
  QJsonObject settings;
  QList<RunPluginRef> plugins;
  QList<RunParticipant> participants;
  qint64 lastSeq = 0;

  const RunParticipant* participant(const QString& userId) const;
  RunParticipant* participant(const QString& userId);
  // starting and active accept state, event and board writes; paused, finished and abandoned refuse them (409 run_not_active).
  bool acceptsWrites() const { return status == QLatin1String("starting") || status == QLatin1String("active"); }
  bool isOver() const { return status == QLatin1String("finished") || status == QLatin1String("abandoned"); }
  bool hasPlugin(const QString& pluginId) const;
};

struct RunEvent {
  qint64 seq = 0;
  QString createdAt;
  QString userId;  // empty for Hub events
  QString userName;
  QString deviceId;
  QString pluginId;  // empty for Hub events
  QString source;    // automatic | manual | hub
  QString type;
  QString text;
  QJsonObject payload;
  QString clientId;
};

// nullopt for an object without a valid run_id (uuid), name or status.
std::optional<RunInfo> parseRun(const QJsonObject& o);
QJsonObject runToJson(const RunInfo& r);
std::optional<RunEvent> parseRunEvent(const QJsonObject& o);
QJsonObject runEventToJson(const RunEvent& e);
QJsonObject participantToJson(const RunParticipant& p);

// Limits of the Hub (contract section 5).
inline constexpr int kRunMaxParticipants = 8;
inline constexpr int kRunMaxEventText = 280;
inline constexpr int kRunMaxEventPayloadBytes = 4 * 1024;
inline constexpr int kRunMaxStateBytes = 64 * 1024;
inline constexpr int kRunMaxBoardBytes = 64 * 1024;

// Slot of the Player's saves for a run: "run-" + the first 8 hex characters of the run id.
QString runSaveSlot(const QString& runId);

}  // namespace uzume
