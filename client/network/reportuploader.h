#pragma once
// ReportUploader: sends one problem report (POST /reports, handshake feature reports_v1) to the active Hub. The
// request is asynchronous (QNetworkAccessManager), so the GUI thread never blocks; the body is built by
// ProblemReport::multipartBody() off the GUI thread.
//
// States (docs/design/legacy/player.md 6d-2):
//   Idle     nothing to do
//   Sending  request in flight, progress(sent, total)
//   Sent     the Hub stored it: reportId()
//   Offline  the Hub is not connected: the report waits and is sent by itself when the connection is back (as long as
//            the caller keeps it, i.e. does not call cancel()/reset())
//   Failed   the Hub refused it or the connection broke: errorText(); send() again with the same body retries

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include "hubconnection.h"

namespace uzume {

class ReportUploader : public QObject {
  Q_OBJECT
 public:
  enum class State { Idle, Sending, Sent, Offline, Failed };
  Q_ENUM(State)

  explicit ReportUploader(HubConnection* connection, QObject* parent = nullptr);

  State state() const { return state_; }
  qint64 reportId() const { return reportId_; }  // Sent
  QString errorCode() const { return errorCode_; }  // Failed: unsupported | too_large | rate_limited | rejected | network | server
  QString errorText() const { return errorText_; }  // Failed: shown to the user
  qint64 bytesSent() const { return sent_; }
  qint64 bytesTotal() const { return total_; }
  bool supported() const;  // connected and the Hub advertises reports_v1

  // Starts (or restarts) the upload of `body` (multipart/form-data) with its Content-Type header value. While the Hub is
  // not connected the state becomes Offline and the upload starts when the connection is back.
  void send(const QByteArray& body, const QByteArray& contentType);
  void cancel();  // Sending/Offline/Failed -> Idle (an upload in flight is aborted)
  void reset();   // any state -> Idle, forgets the body

 signals:
  void stateChanged(uzume::ReportUploader::State state);
  void progress(qint64 sent, qint64 total);

 private:
  void setState(State s);
  void start();
  void onFinished(QNetworkReply* reply);
  void fail(const QString& code, const QString& text);

  QPointer<HubConnection> conn_;
  QPointer<QNetworkReply> reply_;
  QByteArray body_, contentType_;
  State state_ = State::Idle;
  qint64 reportId_ = 0, sent_ = 0, total_ = 0;
  QString errorCode_, errorText_;
  bool cancelled_ = false;
  bool renewed_ = false;  // a 401 was answered by renewing the token once
};

}  // namespace uzume
