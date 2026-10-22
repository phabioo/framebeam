#pragma once
// Runtime environment variables of the app: UZUME_<X> (HUB -> SERVER and PLAYER -> APP inside the name). Before the
// rename the variables were FRAMEBEAM_<X>; the old exact name is still read when the new one is unset, and every old
// name in use logs one warning ("FRAMEBEAM_X is deprecated, use UZUME_Y"). Build and test variables are not covered
// here (no fallback). All functions are header-only so that every library can use them.

#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QDebug>

namespace uzume::env {

// FRAMEBEAM_ name for a UZUME_ name: UZUME_APP_UPDATE_INDEX_URL -> FRAMEBEAM_PLAYER_UPDATE_INDEX_URL,
// UZUME_LOCAL_SERVER_PORT -> FRAMEBEAM_LOCAL_HUB_PORT. Empty if `name` does not start with UZUME_.
inline QByteArray legacyName(const QByteArray& name) {
  if (!name.startsWith("UZUME_")) return {};
  const QList<QByteArray> parts = name.mid(6).split('_');
  QByteArray out = "FRAMEBEAM";
  for (const QByteArray& p : parts) {
    out += '_';
    if (p == "APP") out += "PLAYER";
    else if (p == "SERVER") out += "HUB";
    else out += p;
  }
  return out;
}

// The variable to read for `uzumeName`: the new name, or the old one when only that is set (one warning per name).
inline QByteArray name(const char* uzumeName) {
  const QByteArray current(uzumeName);
  if (qEnvironmentVariableIsSet(uzumeName)) return current;
  const QByteArray old = legacyName(current);
  if (old.isEmpty() || !qEnvironmentVariableIsSet(old.constData())) return current;
  static QMutex mutex;
  static QSet<QByteArray> warned;
  bool first = false;
  {
    QMutexLocker lock(&mutex);
    first = !warned.contains(old);
    warned.insert(old);
  }
  if (first) qWarning().noquote() << old << "is deprecated, use" << current;
  return old;
}

inline QString get(const char* uzumeName) { return qEnvironmentVariable(name(uzumeName).constData()); }
inline QByteArray bytes(const char* uzumeName) { return qgetenv(name(uzumeName).constData()); }
inline bool isSet(const char* uzumeName) { return qEnvironmentVariableIsSet(name(uzumeName).constData()); }
inline int intValue(const char* uzumeName, bool* ok = nullptr) {
  return qEnvironmentVariableIntValue(name(uzumeName).constData(), ok);
}

}  // namespace uzume::env
