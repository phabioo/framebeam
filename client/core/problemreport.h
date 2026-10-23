#pragma once
// "Report a problem" (0.17, docs/design/legacy/player.md 6d): a short description plus a few attachments that go to the user's
// own Hub (POST /reports, feature reports_v1) or into a zip file. This class only builds the report; sending is
// network/reportuploader.h. Everything here is redacted (core/diagbundle.h redact()); never included: passwords, the
// Hub pairing token, ROMs, saves.
//
// Attachments (checkbox rows of the dialog):
//   logs        player-logs.txt        logs of the last 3 days, redacted, newest file first (cap 12 MiB)      on
//   settings    settings.json          settings/player.json without passwords, tokens and secrets             on
//   connection  connection-check.json  result of the last connection check, when one is available             on
//   system      system-info.json       Windows version, GPU, driver                                           off

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace uzume {

class ProblemReport {
 public:
  struct Attachment {
    QString id;        // logs | settings | connection | system
    QString label;     // "Logs of the last 3 days"
    QString detail;    // short note under the label (system info: "Windows version, GPU, driver")
    QString fileName;  // name in the report (the Hub accepts [A-Za-z0-9._-])
    QByteArray data;
    QDateTime time;    // connection check: when it ran
    bool available = true;
    bool included = false;
    qint64 size() const { return data.size(); }
  };

  struct Sources {
    QString dataDir;
    QString logDir;                 // default <dataDir>/logs
    QJsonObject systemInfo;         // diag::offlineInfo()-shaped object (system, emulation, media); only the parts a
                                    // support person needs are kept
    QDateTime now;                  // tests; default current time
    int logDays = 3;
    qint64 maxLogBytes = 12ll << 20;
    QString playerVersion;          // tests; default the build version
    QString os;                     // tests; default QSysInfo::prettyProductName()
  };

  static constexpr qint64 kMaxAttachmentBytes = 25ll << 20;  // the Hub refuses more (413)
  static constexpr int kMaxDescription = 8000;               // characters; the Hub refuses more (400)

  // Reads the logs and settings and builds the attachments. Does file I/O and redaction: call it off the GUI thread.
  static ProblemReport collect(const Sources& sources);

  // Text of the description is plain; the Hub redacts it once more.
  void setDescription(const QString& text) { description_ = text; }
  QString description() const { return description_; }
  bool hasDescription() const { return !description_.trimmed().isEmpty(); }
  bool descriptionTooLong() const { return description_.size() > kMaxDescription; }

  // The event the report is about (optional, "Linked: “Session ended unexpectedly” · today 18:41").
  void setLinkedEvent(const QString& title, const QDateTime& when);
  QString linkedTitle() const { return linkedTitle_; }
  QDateTime linkedTime() const { return linkedTime_; }

  // Result of a connection check (another change adds the check): the JSON is redacted and becomes the attachment
  // "connection", included by default. An empty document removes it again.
  void setConnectionCheckJson(const QByteArray& json, const QDateTime& when);

  const QList<Attachment>& attachments() const { return attachments_; }
  bool setIncluded(const QString& id, bool included);  // false: unknown or unavailable attachment
  qint64 includedBytes() const;                         // sum of the included attachments
  bool tooLarge() const { return includedBytes() > kMaxAttachmentBytes; }
  QStringList includedFileNames() const;                // for "See what is included"; report.txt first in a zip

  // The meta part of POST /reports (ReportMeta).
  QJsonObject metaJson() const;
  // multipart/form-data body of POST /reports: the meta part first, then the included files. *contentType gets the
  // header value with the boundary.
  QByteArray multipartBody(QByteArray* contentType) const;
  // The same content as a local zip: report.txt (description and meta) plus the included files. false + *error.
  bool saveAsZip(const QString& path, QString* error = nullptr) const;
  static QString zipFileName(const QDateTime& when);  // uzume-report-<UTC timestamp>.zip

  // settings/player.json with secret-looking keys replaced and the text redacted (public for tests).
  static QByteArray redactedSettings(const QString& dataDir);
  // system-info.json content from a diag::offlineInfo()-shaped object (public for tests).
  static QByteArray systemInfoJson(const QJsonObject& info);

 private:
  Attachment* find(const QString& id);
  QString reportText() const;  // report.txt of the zip

  QString description_;
  QString linkedTitle_;
  QDateTime linkedTime_;
  QList<Attachment> attachments_;
  QString playerVersion_, os_;
  QDateTime now_;
};

}  // namespace uzume
