#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <optional>

#include "hubconnection.h"

namespace uzume {

// A screenshot of the game page (Uzume 0.19 `Screenshot`): taken in a game, optionally tagged with the run it was played in.
struct ScreenshotInfo {
  QString id;
  QString gameId;
  QString runId;  // empty when the game ran outside a run
  QString userId;
  QString userName;
  QString createdAt;
  int width = 0;
  int height = 0;
  qint64 sizeBytes = 0;
};
std::optional<ScreenshotInfo> parseScreenshot(const QJsonObject& obj);

struct ScreenshotResult {
  bool ok = false;
  int status = 0;
  // "" on success, else the server's code (payload_too_large, invalid_image, not_found, ...), "unreachable" without an answer,
  // "not_connected" when no request could be made.
  QString errorCode;
  QString errorMessage;
  ScreenshotInfo screenshot;          // upload
  QList<ScreenshotInfo> screenshots;  // listGame, listRun (newest first)
  qint64 totalBytes = 0;              // listGame, listRun
  QByteArray content;                 // content: the PNG
  // The request can never succeed with this file: the server refuses the image (400) or its size (413).
  bool rejected() const { return status == 400 || status == 413; }
  // No answer at all (offline, server down, signed out): the same request may work later.
  bool transient() const { return !ok && !rejected() && status != 404; }
};

// Screenshots of the calling person on the connected server. The callback runs on the event loop, never synchronously, and is
// dropped if the ScreenshotApi is destroyed.
class ScreenshotApi : public QObject {
  Q_OBJECT
 public:
  using Callback = std::function<void(const ScreenshotResult&)>;
  explicit ScreenshotApi(HubConnection* connection, QObject* parent = nullptr);

  void upload(const QString& gameId, const QString& runId, const QByteArray& png, Callback cb);  // POST /games/{id}/screenshots
  void listGame(const QString& gameId, Callback cb);   // GET /games/{id}/screenshots (own, newest first)
  void listRun(const QString& runId, Callback cb);     // GET /runs/{id}/screenshots (all members)
  void content(const QString& screenshotId, Callback cb);  // GET /screenshots/{id}/content
  void remove(const QString& screenshotId, Callback cb);   // DELETE /screenshots/{id}

  static constexpr qint64 kMaxBytes = 16ll * 1024 * 1024;  // the server refuses more (413)

 private:
  enum class Expect { None, One, List, Bytes };
  void request(const QByteArray& method, const QString& path, const QByteArray& body, const QByteArray& contentType, Expect expect,
               Callback cb);

  QPointer<HubConnection> conn_;
};

}  // namespace uzume

Q_DECLARE_METATYPE(uzume::ScreenshotInfo)
