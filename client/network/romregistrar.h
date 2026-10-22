#pragma once
// Registers local ROM files with the Hub (0.12 local_roms_v1): POST /games/register, DELETE /games/{id}/registration.
// The Hub only learns hash, size, file name and game identity; the file stays on this device.
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>

#include "hubconnection.h"
#include "hubprotocol.h"
#include "localromindex.h"

namespace uzume {

struct RegisterOutcome {
  QString sha256;
  bool ok = false;
  bool created = false;  // 201: new local-only game; 200: matched an existing game
  GameEntry game;
  QString errorCode;
  QString errorMessage;
};

class RomRegistrar : public QObject {
  Q_OBJECT
 public:
  explicit RomRegistrar(HubConnection* connection, QObject* parent = nullptr);

  bool supported() const;  // connected and the Hub offers local_roms_v1
  // Queues the files (one request at a time). Files with an invalid hash are skipped.
  void registerFiles(const QList<LocalRomFile>& files);
  bool busy() const { return inFlight_ || !queue_.isEmpty(); }
  int pending() const { return static_cast<int>(queue_.size()) + (inFlight_ ? 1 : 0); }
  // Removes the caller's local registrations of the game; the game and its saves stay on the Hub.
  void unregisterGame(const QString& gameId);

  static QJsonObject requestBody(const LocalRomFile& f);
  // The scanned files the Hub does not know yet: not in any game's roms[] and not in `ignoredShas` (files the user
  // removed from the library; they stay out until the file disappears from the folders). One file per hash.
  static QList<LocalRomFile> filesToRegister(const QList<LocalRomFile>& scanned, const QList<GameEntry>& games,
                                             const QSet<QString>& ignoredShas);

 signals:
  void registered(const uzume::RegisterOutcome& outcome);
  void batchFinished(int registered, int failed);  // the queue ran empty
  void unregistered(const QString& gameId, bool ok, const QString& errorMessage);

 private:
  void next();

  QPointer<HubConnection> conn_;
  QList<LocalRomFile> queue_;
  bool inFlight_ = false;
  int okCount_ = 0;
  int failCount_ = 0;
};

}  // namespace uzume

Q_DECLARE_METATYPE(uzume::RegisterOutcome)
