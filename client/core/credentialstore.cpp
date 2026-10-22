#include "credentialstore.h"

#include <QLoggingCategory>
#include <QMutexLocker>
#include <mutex>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>

#include <string>
#endif

namespace uzume {

Q_LOGGING_CATEGORY(lcCred, "uzume.credentials")

QString credentialTarget(const QString& hubId, const QString& deviceId) {
  return QStringLiteral("Uzume/%1/%2").arg(hubId, deviceId);
}

MemoryCredentialStore::MemoryCredentialStore() {
  static std::once_flag warned;
  std::call_once(warned, [] {
    qCWarning(lcCred) << "No OS credential store on this platform: credentials are kept in memory only, not persisted.";
  });
}

bool MemoryCredentialStore::write(const QString& target, const QString& secret) {
  QMutexLocker lock(&mutex_);
  items_.insert(target, secret);
  return true;
}

std::optional<QString> MemoryCredentialStore::read(const QString& target) const {
  QMutexLocker lock(&mutex_);
  const auto it = items_.constFind(target);
  if (it == items_.constEnd()) {
    return std::nullopt;
  }
  return it.value();
}

bool MemoryCredentialStore::remove(const QString& target) {
  QMutexLocker lock(&mutex_);
  items_.remove(target);
  return true;
}

#ifdef Q_OS_WIN
bool WindowsCredentialStore::write(const QString& target, const QString& secret) {
  const std::wstring wtarget = target.toStdWString();
  const std::wstring wuser = L"Uzume";
  const QByteArray blob = secret.toUtf8();
  CREDENTIALW cred = {};
  cred.Type = CRED_TYPE_GENERIC;
  cred.TargetName = const_cast<LPWSTR>(wtarget.c_str());
  cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
  cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(blob.constData()));
  cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
  cred.UserName = const_cast<LPWSTR>(wuser.c_str());
  return CredWriteW(&cred, 0) == TRUE;
}

std::optional<QString> WindowsCredentialStore::read(const QString& target) const {
  const std::wstring wtarget = target.toStdWString();
  PCREDENTIALW cred = nullptr;
  if (CredReadW(wtarget.c_str(), CRED_TYPE_GENERIC, 0, &cred) != TRUE || cred == nullptr) {
    return std::nullopt;
  }
  const QString secret = QString::fromUtf8(reinterpret_cast<const char*>(cred->CredentialBlob),
                                           static_cast<qsizetype>(cred->CredentialBlobSize));
  CredFree(cred);
  return secret;
}

bool WindowsCredentialStore::remove(const QString& target) {
  const std::wstring wtarget = target.toStdWString();
  if (CredDeleteW(wtarget.c_str(), CRED_TYPE_GENERIC, 0) == TRUE) {
    return true;
  }
  return GetLastError() == ERROR_NOT_FOUND;
}
#endif

namespace {
const QString kNewPrefix = QStringLiteral("Uzume/");
const QString kOldPrefix = QStringLiteral("FrameBeam/");
}  // namespace

std::optional<QString> readCredential(CredentialStore& store, const QString& ref) {
  if (ref.isEmpty()) return std::nullopt;
  QString rest;
  if (ref.startsWith(kNewPrefix)) rest = ref.mid(kNewPrefix.size());
  else if (ref.startsWith(kOldPrefix)) rest = ref.mid(kOldPrefix.size());
  else return store.read(ref);
  const QString newTarget = kNewPrefix + rest;
  if (const auto v = store.read(newTarget)) return v;
  const auto old = store.read(kOldPrefix + rest);
  if (old && store.write(newTarget, *old)) {
    qCInfo(lcCred) << "Credential copied to the new target name (the old entry is kept)";
  }
  return old;
}

bool removeCredential(CredentialStore& store, const QString& ref) {
  QString rest;
  if (ref.startsWith(kNewPrefix)) rest = ref.mid(kNewPrefix.size());
  else if (ref.startsWith(kOldPrefix)) rest = ref.mid(kOldPrefix.size());
  else return store.remove(ref);
  const bool a = store.remove(kNewPrefix + rest);
  const bool b = store.remove(kOldPrefix + rest);
  return a && b;
}

std::unique_ptr<CredentialStore> createDefaultCredentialStore() {
#ifdef Q_OS_WIN
  return std::make_unique<WindowsCredentialStore>();
#else
  return std::make_unique<MemoryCredentialStore>();
#endif
}

}  // namespace uzume
