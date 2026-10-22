#include "reportuploader.h"

#include <QBuffer>
#include <QTimer>

namespace uzume {

namespace {
constexpr const char* kFeature = "reports_v1";
}

ReportUploader::ReportUploader(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {
  if (conn_) {
    // The report waits for the Hub: when the connection is back it goes out by itself.
    connect(conn_, &HubConnection::stateChanged, this, [this](HubConnection::State s) {
      if (s == HubConnection::State::Connected && state_ == State::Offline) start();
    });
  }
}

bool ReportUploader::supported() const {
  return conn_ && conn_->state() == HubConnection::State::Connected && conn_->hubHasFeature(QString::fromLatin1(kFeature));
}

void ReportUploader::setState(State s) {
  if (state_ == s) return;
  state_ = s;
  emit stateChanged(s);
}

void ReportUploader::send(const QByteArray& body, const QByteArray& contentType) {
  if (state_ == State::Sending) return;
  body_ = body;
  contentType_ = contentType;
  reportId_ = 0;
  renewed_ = false;
  errorCode_.clear();
  errorText_.clear();
  start();
}

void ReportUploader::start() {
  if (body_.isEmpty()) return;
  if (!conn_ || conn_->state() != HubConnection::State::Connected) {
    setState(State::Offline);
    return;
  }
  if (!conn_->hubHasFeature(QString::fromLatin1(kFeature))) {
    fail(QStringLiteral("unsupported"),
         tr("This Hub is too old to receive problem reports. Save the report as a file and send it to its admin."));
    return;
  }
  auto* buf = new QBuffer;
  buf->setData(body_);
  buf->open(QIODevice::ReadOnly);
  QNetworkReply* reply = conn_->authorizedSendStream("POST", QStringLiteral("/reports"), buf, {}, contentType_);
  if (reply == nullptr) {
    delete buf;
    setState(State::Offline);
    return;
  }
  buf->setParent(reply);  // lives exactly as long as the request
  reply_ = reply;
  sent_ = 0;
  total_ = body_.size();
  errorCode_.clear();
  errorText_.clear();
  setState(State::Sending);
  connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
    sent_ = sent;
    if (total > 0) total_ = total;
    emit progress(sent_, total_);
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply]() { onFinished(reply); });
}

void ReportUploader::fail(const QString& code, const QString& text) {
  errorCode_ = code;
  errorText_ = text;
  setState(State::Failed);
}

void ReportUploader::onFinished(QNetworkReply* reply) {
  const HttpResult r = HubHttp::resultOf(reply);
  reply->deleteLater();
  if (reply_ != reply) return;  // cancelled meanwhile
  reply_.clear();
  if (r.ok()) {
    reportId_ = r.json().value(QStringLiteral("id")).toVariant().toLongLong();
    sent_ = total_;
    emit progress(sent_, total_);
    setState(State::Sent);
    return;
  }
  if (r.status == 401 && conn_) {
    conn_->noteUnauthorized();  // the token is renewed; the report goes out again once, or waits for the connection
    if (!renewed_) {
      renewed_ = true;
      QTimer::singleShot(0, this, [this]() {
        if (state_ == State::Sending) start();
      });
      return;
    }
    fail(QStringLiteral("auth"), tr("The sign-in to the Hub expired. Try again."));
    return;
  }
  if (r.status == 413) {
    fail(QStringLiteral("too_large"), tr("The report is larger than the Hub accepts (25 MB). Uncheck the logs and try again."));
  } else if (r.status == 429) {
    fail(QStringLiteral("rate_limited"), tr("Too many reports from this device. Try again later or save the report as a file."));
  } else if (r.status == 400 || r.status == 403) {
    fail(QStringLiteral("rejected"), r.apiErrorMessage.isEmpty() ? tr("The Hub did not accept the report.") : r.apiErrorMessage);
  } else if (r.status == 404) {
    fail(QStringLiteral("unsupported"), tr("This Hub does not receive problem reports. Save the report as a file."));
  } else if (r.status >= 500) {
    fail(QStringLiteral("server"), tr("The Hub answered with an error (%1). Try again.").arg(r.status));
  } else {
    const int pct = total_ > 0 ? static_cast<int>(sent_ * 100 / total_) : 0;
    fail(QStringLiteral("network"), tr("Upload stopped at %1 % (connection lost). Your text is kept.").arg(pct));
  }
}

void ReportUploader::cancel() {
  if (reply_) {
    QNetworkReply* reply = reply_;
    reply_.clear();
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }
  setState(State::Idle);
}

void ReportUploader::reset() {
  cancel();
  body_.clear();
  contentType_.clear();
  reportId_ = 0;
  sent_ = total_ = 0;
  errorCode_.clear();
  errorText_.clear();
}

}  // namespace uzume
