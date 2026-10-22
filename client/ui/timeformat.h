#pragma once
// One date/time formatting helper for every list and row of the Player (design round 3, sheet 0-2 "Dates and times"):
// just now / 5 min ago / today 18:52 / yesterday 18:52 / 16 Oct / 3 Mar 2025. Never seconds in lists; the full
// timestamp ("16 Oct 2026, 18:52:07") is for tooltips. Month names are fixed English (the UI language is English).
#include <QDateTime>
#include <QString>

namespace uzume::ui {

class TimeFormat {
 public:
  // `now` is injectable for tests.
  static QString when(const QDateTime& t, const QDateTime& now = QDateTime::currentDateTime());
  static QString full(const QDateTime& t);
  // ISO 8601 text (Hub timestamps) -> when() / full(); empty for invalid text.
  static QString whenIso(const QString& iso, const QDateTime& now = QDateTime::currentDateTime());
  static QString fullIso(const QString& iso);
};

}  // namespace uzume::ui
