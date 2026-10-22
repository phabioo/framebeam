#pragma once
// Runtime environment variables of the app: UZUME_<X> (SERVER and APP inside the name). Build and test variables are
// read directly. All functions are header-only so that every library can use them.

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace uzume::env {

inline QString get(const char* name) { return qEnvironmentVariable(name); }
inline QByteArray bytes(const char* name) { return qgetenv(name); }
inline bool isSet(const char* name) { return qEnvironmentVariableIsSet(name); }
inline int intValue(const char* name, bool* ok = nullptr) { return qEnvironmentVariableIntValue(name, ok); }

}  // namespace uzume::env
