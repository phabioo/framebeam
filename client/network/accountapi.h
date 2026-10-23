#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

#include "hubconnection.h"

namespace uzume {

// The signed-in person's account as seen from this device (`getAccount`, feature accounts_v1).
struct AccountInfo {
  QString userId;
  QString username;     // the sign-in name (not the display name)
  QString displayName;
  QString role;         // "admin" | "user"
  bool hasPassword = false;
  bool passwordRequired = false;      // true for admins and while the server requires passwords
  bool mustChangePassword = false;    // an admin reset the password
  QDateTime confirmedUntil;           // invalid = not confirmed
  QDateTime confirmLockedUntil;       // invalid = not locked
  bool valid() const { return !userId.isEmpty(); }
  bool isAdmin() const { return role == QLatin1String("admin"); }
};

// One device of the signed-in person (`listAccountDevices`).
struct AccountDeviceInfo {
  QString deviceId;
  QString name;
  QString platform;
  QString arch;
  QString appVersion;
  QDateTime createdAt;
  QDateTime lastSeenAt;  // invalid = never
  QString signInKind;    // approval | invite | password | local
  QString status;        // trusted | revoked
  QString installId;
  bool current = false;  // the device of this request
  bool revoked() const { return status == QLatin1String("revoked"); }
};

std::optional<AccountInfo> parseAccount(const QJsonObject& obj);
std::optional<AccountDeviceInfo> parseAccountDevice(const QJsonObject& obj);

struct AccountApiResult {
  bool ok = false;
  int status = 0;
  // "" on success, otherwise the server's code (display_name_taken, password_required, password_not_set,
  // invalid_credentials, bad_request, rate_limited, not_found, ...), "unreachable" without an answer, "not_connected"
  // when no request could be made.
  QString errorCode;
  QString errorMessage;
  int retryAfter = 0;               // seconds, only with rate_limited
  AccountInfo account;              // getAccount, updateDisplayName
  QList<AccountDeviceInfo> devices; // listDevices
  int signedOut = 0;                // signOutAll
};

// Account API of the connected server (tag `account`, feature accounts_v1) over the authenticated HubConnection. The
// callback runs on the event loop, never synchronously, and is dropped if the AccountApi is destroyed. Passwords are
// never logged.
class AccountApi : public QObject {
  Q_OBJECT
 public:
  using Callback = std::function<void(const AccountApiResult&)>;
  explicit AccountApi(HubConnection* connection, QObject* parent = nullptr);

  void getAccount(Callback cb);
  void updateDisplayName(const QString& displayName, Callback cb);
  // `currentPassword` is required when the account has a password (empty = not sent). 400 bad_request for a new
  // password under 12 characters, 401 invalid_credentials, 429 rate_limited.
  void setPassword(const QString& currentPassword, const QString& newPassword, Callback cb);
  // 403 password_required (admins, or the server requires passwords), 401 invalid_credentials.
  void removePassword(const QString& currentPassword, Callback cb);
  void listDevices(Callback cb);
  void signOutDevice(const QString& deviceId, Callback cb);
  void signOutAll(bool includeCurrent, Callback cb);

 private:
  enum class Expect { None, Account, Devices, SignedOut };
  void request(const QByteArray& method, const QString& path, const QJsonObject& body, bool hasBody, Expect expect, Callback cb);

  QPointer<HubConnection> conn_;
};

}  // namespace uzume
