#include "filelog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

namespace framebeam::filelog {

namespace {

QMutex g_mutex;
QFile* g_file = nullptr;
QString g_path;
qint64 g_max = 5 * 1024 * 1024;
QStringList g_pending;  // lines before the directory is known
QtMessageHandler g_previous = nullptr;
bool g_installed = false;
constexpr int kPendingMax = 1000;

QString numbered(const QString& logPath, int n) {
  const QFileInfo fi(logPath);
  return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QLatin1Char('.') + QString::number(n) + QLatin1Char('.') + fi.suffix();
}

void writeLine(const QString& line) {  // g_mutex held
  if (g_file == nullptr) {
    if (g_pending.size() < kPendingMax) g_pending.append(line);
    return;
  }
  if (g_file->size() > g_max) {
    g_file->close();
    rotate(g_path);
    g_file->open(QIODevice::WriteOnly | QIODevice::Append);
  }
  g_file->write(line.toUtf8());
  g_file->write("\n");
  g_file->flush();
}

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
  {
    QMutexLocker lock(&g_mutex);
    writeLine(formatLine(type, QString::fromLatin1(ctx.category != nullptr ? ctx.category : "default"), msg));
  }
  if (g_previous != nullptr) g_previous(type, ctx, msg);
}

}  // namespace

QString formatLine(QtMsgType type, const QString& category, const QString& message) {
  QChar level = QLatin1Char('I');
  switch (type) {
    case QtDebugMsg: level = QLatin1Char('D'); break;
    case QtInfoMsg: level = QLatin1Char('I'); break;
    case QtWarningMsg: level = QLatin1Char('W'); break;
    case QtCriticalMsg: level = QLatin1Char('E'); break;
    case QtFatalMsg: level = QLatin1Char('F'); break;
  }
  QString m = message;
  m.replace(QLatin1Char('\n'), QLatin1String("\\n"));
  return QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + QLatin1String(" [") + level + QLatin1String("] ") +
         category + QLatin1String(": ") + m;
}

void rotate(const QString& logPath, int keep) {
  QFile::remove(numbered(logPath, keep));
  for (int i = keep - 1; i >= 1; --i) {
    if (QFileInfo::exists(numbered(logPath, i))) QFile::rename(numbered(logPath, i), numbered(logPath, i + 1));
  }
  if (QFileInfo::exists(logPath)) QFile::rename(logPath, numbered(logPath, 1));
}

void install() {
  QMutexLocker lock(&g_mutex);
  if (g_installed) return;
  g_installed = true;
  g_previous = qInstallMessageHandler(handler);
}

bool setDirectory(const QString& dataDir, qint64 maxBytes) {
  QMutexLocker lock(&g_mutex);
  if (g_file != nullptr || dataDir.isEmpty()) return g_file != nullptr;
  g_max = maxBytes;
  const QString dir = QDir(dataDir).filePath(QStringLiteral("logs"));
  if (!QDir().mkpath(dir)) return false;
  const QString p = QDir(dir).filePath(QStringLiteral("player.log"));
  rotate(p);
  auto* f = new QFile(p);
  if (!f->open(QIODevice::WriteOnly | QIODevice::Append)) {
    delete f;
    g_pending.clear();
    return false;
  }
  g_file = f;
  g_path = p;
  const QStringList pending = g_pending;
  g_pending.clear();
  for (const QString& l : pending) writeLine(l);
  return true;
}

QString path() {
  QMutexLocker lock(&g_mutex);
  return g_path;
}

}  // namespace framebeam::filelog
