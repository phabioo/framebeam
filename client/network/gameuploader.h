#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include "hubconnection.h"
#include "hubprotocol.h"

namespace framebeam {

struct UploadResult {
  enum class Kind {
    Created,    // 201: game created (`game`)
    Duplicate,  // 409: ROM already in the library (`existingGameId`)
    Forbidden,  // 403: uploads are not allowed for this user (uploads_disabled)
    TooLarge,   // 413
    Rejected,   // 400: the Hub rejected the file (unknown type, invalid name, ...)
    Failed      // network error, not connected, unreadable file, ...
  };
  Kind kind = Kind::Failed;
  GameEntry game;
  QString existingGameId;
  QString fileName;
  QString errorCode;
  QString errorMessage;
  int status = 0;
};

// POST /games: streamed raw upload of a ROM file (the file is never read into memory as a whole). One upload at a time.
class GameUploader : public QObject {
  Q_OBJECT
 public:
  explicit GameUploader(HubConnection* connection, QObject* parent = nullptr);

  bool busy() const { return reply_ != nullptr; }
  // Starts the upload. The result always arrives via finished() (also for immediate errors, queued).
  // False if an upload is already running.
  bool upload(const QString& filePath, const QString& title = QString());
  void cancel();

 signals:
  void progress(qint64 sent, qint64 total);
  void finished(const framebeam::UploadResult& result);

 private:
  void failLater(const QString& fileName, const QString& code, const QString& message);

  QPointer<HubConnection> conn_;
  QPointer<QNetworkReply> reply_;
  bool cancelled_ = false;
  QString fileName_;
};

}  // namespace framebeam

Q_DECLARE_METATYPE(framebeam::UploadResult)
