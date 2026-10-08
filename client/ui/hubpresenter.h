#pragma once
// HubPresenter: QML data maps of the connection and pairing screens (Hub cards with their status, pairing phase and
// fingerprint). Read-only view of the HubConnection / ProfileStore state of PlayerController.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "hubconnection.h"
#include "profilestore.h"

namespace framebeam::ui {

// Address without the http(s):// prefix.
QString trimmedScheme(const QString& address);

class HubPresenter : public QObject {
  Q_OBJECT

 public:
  struct Deps {
    HubConnection* conn;
    ProfileStore* profiles;
    const QString& lastError;
    const bool& inviteBusy;
  };

  explicit HubPresenter(const Deps& d, QObject* parent = nullptr);

  QVariantList hubs() const;
  QVariantMap pairing() const;
  static QString formatFingerprint(const QString& fp);
  static QString platformLabel(const QString& platform, const QString& arch);

 private:
  QVariantMap hubCard(const HubProfile& p) const;

  HubConnection* conn_;
  ProfileStore* profiles_;
  const QString& lastError_;
  const bool& inviteBusy_;
};

}  // namespace framebeam::ui
