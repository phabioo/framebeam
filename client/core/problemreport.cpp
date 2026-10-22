#include "problemreport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSysInfo>
#include <QUuid>

#include "diagbundle.h"
#include "playersettings.h"
#include "version.h"

namespace uzume {

namespace {

const QRegularExpression& secretKey() {
  static const QRegularExpression re(
      QStringLiteral("pass(word|wd|phrase)?|secret|token|credential|api[_-]?key|cookie|private[_-]?key|signing[_-]?key|invite|nonce"),
      QRegularExpression::CaseInsensitiveOption);
  return re;
}

QJsonValue scrub(const QJsonValue& v) {
  if (v.isObject()) {
    QJsonObject out;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
      out.insert(it.key(), secretKey().match(it.key()).hasMatch() ? QJsonValue(QStringLiteral("[redacted]")) : scrub(it.value()));
    }
    return out;
  }
  if (v.isArray()) {
    QJsonArray out;
    for (const QJsonValue& x : v.toArray()) out.append(scrub(x));
    return out;
  }
  if (v.isString()) return QString::fromUtf8(diag::redact(v.toString().toUtf8()));  // value by value: the JSON stays valid
  return v;
}

QJsonObject pick(const QJsonObject& from, std::initializer_list<const char*> keys) {
  QJsonObject out;
  for (const char* k : keys) {
    const QString key = QString::fromLatin1(k);
    if (from.contains(key)) out.insert(key, from.value(key));
  }
  return out;
}

QByteArray jsonBytes(const QJsonObject& o) { return QJsonDocument(scrub(o).toObject()).toJson(QJsonDocument::Indented); }

QString isoUtc(const QDateTime& t) { return t.toUTC().toString(Qt::ISODate); }

}  // namespace

ProblemReport ProblemReport::collect(const Sources& s) {
  ProblemReport r;
  r.now_ = s.now.isValid() ? s.now : QDateTime::currentDateTime();
  r.playerVersion_ = s.playerVersion.isEmpty() ? QString::fromUtf8(playerVersion().data(), static_cast<qsizetype>(playerVersion().size())) : s.playerVersion;
  r.os_ = s.os.isEmpty() ? QSysInfo::prettyProductName() : s.os;

  Attachment logs;
  logs.id = QStringLiteral("logs");
  logs.label = QStringLiteral("Logs of the last %1 days").arg(s.logDays);
  logs.fileName = QStringLiteral("player-logs.txt");
  logs.data = diag::recentLogs(s.logDir.isEmpty() ? QDir(s.dataDir).filePath(QStringLiteral("logs")) : s.logDir, s.logDays,
                               s.maxLogBytes, s.now);
  logs.available = !logs.data.isEmpty();
  logs.included = logs.available;

  Attachment settings;
  settings.id = QStringLiteral("settings");
  settings.label = QStringLiteral("Settings");
  settings.detail = QStringLiteral("no passwords, no Hub token");
  settings.fileName = QStringLiteral("settings.json");
  settings.data = redactedSettings(s.dataDir);
  settings.included = true;

  Attachment connection;
  connection.id = QStringLiteral("connection");
  connection.label = QStringLiteral("Connection check");
  connection.fileName = QStringLiteral("connection-check.json");
  connection.available = false;

  Attachment system;
  system.id = QStringLiteral("system");
  system.label = QStringLiteral("System info");
  system.detail = QStringLiteral("Windows version, GPU, driver");
  system.fileName = QStringLiteral("system-info.json");
  system.data = systemInfoJson(s.systemInfo);
  system.included = false;  // opt-in

  r.attachments_ = {logs, settings, connection, system};
  return r;
}

QByteArray ProblemReport::redactedSettings(const QString& dataDir) {
  QJsonObject o;
  QFile f(PlayerSettings(dataDir).filePath());
  if (f.open(QIODevice::ReadOnly)) {
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isObject()) o = doc.object();
  }
  return jsonBytes(o);
}

QByteArray ProblemReport::systemInfoJson(const QJsonObject& info) {
  QJsonObject out;
  out.insert(QStringLiteral("generated_at"), isoUtc(QDateTime::currentDateTime()));
  out.insert(QStringLiteral("player"), info.value(QStringLiteral("player")).toObject());
  // No data directory, Hubs or controllers: only what a support person needs for rendering and decoding problems.
  out.insert(QStringLiteral("system"),
             pick(info.value(QStringLiteral("system")).toObject(),
                  {"os", "kernel", "arch", "cpu_threads", "gpu", "qt_version", "free_bytes"}));
  out.insert(QStringLiteral("emulation"),
             pick(info.value(QStringLiteral("emulation")).toObject(), {"hw_render", "gl_renderer", "gl_version"}));
  out.insert(QStringLiteral("media"),
             pick(info.value(QStringLiteral("media")).toObject(), {"encoders_available", "encoder_last_chosen", "forced_encoder"}));
  return jsonBytes(out);
}

void ProblemReport::setLinkedEvent(const QString& title, const QDateTime& when) {
  linkedTitle_ = title.simplified();
  linkedTime_ = linkedTitle_.isEmpty() ? QDateTime() : when;
}

ProblemReport::Attachment* ProblemReport::find(const QString& id) {
  for (Attachment& a : attachments_) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

void ProblemReport::setConnectionCheckJson(const QByteArray& json, const QDateTime& when) {
  Attachment* a = find(QStringLiteral("connection"));
  if (a == nullptr) return;
  if (json.trimmed().isEmpty()) {
    a->data.clear();
    a->available = false;
    a->included = false;
    return;
  }
  a->data = diag::redact(json);
  a->time = when;
  a->available = true;
  a->included = true;
}

bool ProblemReport::setIncluded(const QString& id, bool included) {
  Attachment* a = find(id);
  if (a == nullptr || !a->available) return false;
  a->included = included;
  return true;
}

qint64 ProblemReport::includedBytes() const {
  qint64 n = 0;
  for (const Attachment& a : attachments_) {
    if (a.available && a.included) n += a.size();
  }
  return n;
}

QStringList ProblemReport::includedFileNames() const {
  QStringList out;
  for (const Attachment& a : attachments_) {
    if (a.available && a.included) out.append(a.fileName);
  }
  return out;
}

QJsonObject ProblemReport::metaJson() const {
  QJsonObject m{{QStringLiteral("description"), description_.trimmed()},
                {QStringLiteral("player_version"), playerVersion_},
                {QStringLiteral("os"), os_}};
  if (!linkedTitle_.isEmpty()) {
    QJsonObject ev{{QStringLiteral("title"), linkedTitle_}};
    if (linkedTime_.isValid()) ev.insert(QStringLiteral("time"), isoUtc(linkedTime_));
    m.insert(QStringLiteral("linked_event"), ev);
  }
  return m;
}

QByteArray ProblemReport::multipartBody(QByteArray* contentType) const {
  const QByteArray boundary = "----UzumeReport" + QUuid::createUuid().toByteArray(QUuid::Id128);
  if (contentType != nullptr) *contentType = "multipart/form-data; boundary=" + boundary;
  QByteArray body;
  body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"meta\"\r\nContent-Type: application/json\r\n\r\n";
  body += QJsonDocument(metaJson()).toJson(QJsonDocument::Compact) + "\r\n";
  for (const Attachment& a : attachments_) {
    if (!a.available || !a.included) continue;
    body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"files\"; filename=\"" + a.fileName.toUtf8() +
            "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    body += a.data + "\r\n";
  }
  body += "--" + boundary + "--\r\n";
  return body;
}

QString ProblemReport::reportText() const {
  QString t = QStringLiteral("Uzume problem report\nCreated: %1\nPlayer: %2\nSystem: %3\n")
                  .arg(isoUtc(now_), playerVersion_, os_);
  if (!linkedTitle_.isEmpty()) {
    t += QStringLiteral("Linked to: %1").arg(linkedTitle_);
    if (linkedTime_.isValid()) t += QStringLiteral(" (%1)").arg(isoUtc(linkedTime_));
    t += QLatin1Char('\n');
  }
  t += QLatin1Char('\n') + description_.trimmed() + QLatin1Char('\n');
  return t;
}

bool ProblemReport::saveAsZip(const QString& path, QString* error) const {
  QList<QPair<QString, QByteArray>> entries;
  entries.append({QStringLiteral("report.txt"), diag::redact(reportText().toUtf8())});
  for (const Attachment& a : attachments_) {
    if (a.available && a.included) entries.append({a.fileName, a.data});
  }
  return diag::writeZip(path, entries, now_.toUTC(), error);
}

QString ProblemReport::zipFileName(const QDateTime& when) {
  return QStringLiteral("uzume-report-") + when.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'")) +
         QStringLiteral(".zip");
}

}  // namespace uzume
