#pragma once

#include <QString>
#include <QtGlobal>
#include <memory>
#include <optional>

#include <QHash>
#include <QMutex>

namespace framebeam {

// Storage for the long-lived device credential. Never put secrets in files or logs.
class CredentialStore {
 public:
  virtual ~CredentialStore() = default;
  virtual bool write(const QString& target, const QString& secret) = 0;
  virtual std::optional<QString> read(const QString& target) const = 0;
  // true even if the entry did not exist.
  virtual bool remove(const QString& target) = 0;
  virtual QString backendName() const = 0;
  virtual bool isPersistent() const = 0;
};

// Target/reference in the store: "FrameBeam/<hub_id>/<device_id>".
QString credentialTarget(const QString& hubId, const QString& deviceId);

// Memory only (Linux/macOS are not part of the PoC). Warns once in the log.
class MemoryCredentialStore final : public CredentialStore {
 public:
  MemoryCredentialStore();
  bool write(const QString& target, const QString& secret) override;
  std::optional<QString> read(const QString& target) const override;
  bool remove(const QString& target) override;
  QString backendName() const override { return QStringLiteral("memory"); }
  bool isPersistent() const override { return false; }

 private:
  mutable QMutex mutex_;
  QHash<QString, QString> items_;
};

#ifdef Q_OS_WIN
// Windows Credential Manager (CredWriteW/CredReadW/CredDeleteW).
class WindowsCredentialStore final : public CredentialStore {
 public:
  bool write(const QString& target, const QString& secret) override;
  std::optional<QString> read(const QString& target) const override;
  bool remove(const QString& target) override;
  QString backendName() const override { return QStringLiteral("windows-credential-manager"); }
  bool isPersistent() const override { return true; }
};
#endif

std::unique_ptr<CredentialStore> createDefaultCredentialStore();

}  // namespace framebeam
