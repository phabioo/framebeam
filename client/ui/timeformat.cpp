#include "timeformat.h"

#include <QCoreApplication>
#include <QLocale>

namespace framebeam::ui {

namespace {
QString dayMonth(const QDate& d) { return QStringLiteral("%1 %2").arg(d.day()).arg(QLocale::c().monthName(d.month(), QLocale::ShortFormat)); }
}  // namespace

QString TimeFormat::when(const QDateTime& t, const QDateTime& now) {
  if (!t.isValid()) return QCoreApplication::translate("TimeFormat", "unknown time");
  const QDateTime local = t.toLocalTime();
  const QDateTime ref = now.toLocalTime();
  const qint64 secs = local.secsTo(ref);
  if (secs < 60) return QCoreApplication::translate("TimeFormat", "just now");  // also timestamps slightly in the future
  if (secs < 3600) return QCoreApplication::translate("TimeFormat", "%1 min ago").arg(secs / 60);
  const QString hm = local.time().toString(QStringLiteral("HH:mm"));
  if (local.date() == ref.date()) return QCoreApplication::translate("TimeFormat", "today %1").arg(hm);
  if (local.date() == ref.date().addDays(-1)) return QCoreApplication::translate("TimeFormat", "yesterday %1").arg(hm);
  if (local.date().year() == ref.date().year()) return dayMonth(local.date());
  return QStringLiteral("%1 %2").arg(dayMonth(local.date())).arg(local.date().year());
}

QString TimeFormat::full(const QDateTime& t) {
  if (!t.isValid()) return {};
  const QDateTime local = t.toLocalTime();
  return QStringLiteral("%1 %2, %3").arg(dayMonth(local.date())).arg(local.date().year()).arg(local.time().toString(QStringLiteral("HH:mm:ss")));
}

QString TimeFormat::whenIso(const QString& iso, const QDateTime& now) {
  const QDateTime t = QDateTime::fromString(iso, Qt::ISODate);
  return t.isValid() ? when(t, now) : QString();
}

QString TimeFormat::fullIso(const QString& iso) { return full(QDateTime::fromString(iso, Qt::ISODate)); }

}  // namespace framebeam::ui
