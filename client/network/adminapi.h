#pragma once
// AdminApi: generic JSON / file requests under /admin/... and /account/... (relative to api_base) on top of the
// authenticated requests of HubConnection. No endpoint knowledge: the admin pages (Users, Systems and cores, Plugins,
// Reports, ...) build their paths and bodies and read the parsed answer. The token never leaves HubConnection; a 401 is
// reported to it (noteUnauthorized), the caller still gets the result. Passwords and tokens are never logged here.
//
// Three calls, one result type:
//   request()  JSON in, JSON out (any method).
//   download() GET (or POST, e.g. the diagnostics bundle) streamed into a file: written to a temporary file next to the target and renamed on success, a
//              failed or cancelled transfer leaves nothing behind.
//   upload()   local file as streamed request body (plugin or firmware files).
// An error answer of a download (status not 2xx) is read as a normal JSON error, not written to the file.

#include <QByteArray>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <functional>

#include "hubconnection.h"

namespace uzume {

class AdminApi : public QObject {
  Q_OBJECT
 public:
  struct Result {
    bool ok = false;            // 2xx
    int status = 0;             // HTTP status, 0 = no answer
    bool networkError = false;  // no HTTP answer (offline, certificate mismatch, aborted)
    QVariant data;              // parsed JSON object (QVariantMap) or array (QVariantList); invalid for an empty / non-JSON body
    QString code;               // error.code of an error answer; "network" for a network error, "not_connected" without a connection
    QString message;            // error.message, or the network error text
    int retryAfter = 0;         // error.retry_after seconds (429), 0 = not sent
    QString filePath;           // download(): the written file on success
  };
  using Done = std::function<void(const Result&)>;

  explicit AdminApi(HubConnection* conn, QObject* parent = nullptr);

  // Only paths under /admin/ or /account/ (query allowed), no "..", no backslash, no "//" and no fragment.
  static bool pathAllowed(const QString& path);
  static bool methodAllowed(const QString& method);
  // "file:///tmp/a.txt" or "/tmp/a.txt" -> local path.
  static QString localPath(const QString& fileUrlOrPath);

  // `body` invalid = no body; a map or list is sent as JSON. `done` is called exactly once (also for a refusal, from the
  // event loop). The returned reply (null for a refusal) can be aborted; done is then called with a network error.
  QPointer<QNetworkReply> request(const QString& method, const QString& path, const QVariant& body, Done done);
  QPointer<QNetworkReply> download(const QString& path, const QString& targetFile, Done done, const QString& method = QStringLiteral("GET"));
  QPointer<QNetworkReply> upload(const QString& method, const QString& path, const QString& sourceFile,
                                 const QString& contentType, Done done);

  static Result resultOf(QNetworkReply* reply);

 private:
  void refuse(Done done, const QString& code, const QString& message);
  void finish(QNetworkReply* reply, Done done);

  HubConnection* conn_;
};

}  // namespace uzume
