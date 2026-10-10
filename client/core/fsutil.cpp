#include "fsutil.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLoggingCategory>

namespace framebeam::fsutil {

Q_LOGGING_CATEGORY(lcFsUtil, "framebeam.fsutil")

QString quarantineCorruptFile(const QString& path, const QString& reason) {
  const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  QString target = QStringLiteral("%1.corrupt-%2").arg(path, stamp);
  for (int i = 2; QFileInfo::exists(target) && i < 100; ++i) {
    target = QStringLiteral("%1.corrupt-%2-%3").arg(path, stamp).arg(i);
  }
  if (QFileInfo::exists(target) || !QFile::rename(path, target)) {
    qCWarning(lcFsUtil) << QFileInfo(path).fileName() << "is not valid (" << reason << ") and could not be moved aside";
    return {};
  }
  qCWarning(lcFsUtil) << QFileInfo(path).fileName() << "is not valid (" << reason << "); kept as" << QFileInfo(target).fileName()
                      << "and started with defaults";
  return target;
}

QJsonObject readJsonObject(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QByteArray data = f.readAll();
  f.close();
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
  if (err.error == QJsonParseError::NoError && doc.isObject()) {
    return doc.object();
  }
  quarantineCorruptFile(path, err.error != QJsonParseError::NoError ? err.errorString() : QStringLiteral("not a JSON object"));
  return {};
}

bool replaceFile(const QString& from, const QString& to) {
  if (!QFileInfo(from).isFile()) {
    return false;
  }
  if (QFileInfo::exists(to) && !QFile::remove(to)) {
    return false;
  }
  if (QFile::rename(from, to)) {
    return true;
  }
  if (QFile::copy(from, to)) {
    QFile::remove(from);
    return true;
  }
  QFile::remove(to);  // partial copy
  return false;
}

QString envPath(const char* name) { return qEnvironmentVariable(name); }

}  // namespace framebeam::fsutil
