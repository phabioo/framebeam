#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <optional>

#include "hubconnection.h"

namespace framebeam {

inline constexpr qint64 kMaxPluginDownloadBytes = 32ll * 1024 * 1024;  // same limit as a .fbplugin file

// One offered plugin version (GET /plugins, handshake feature plugins_v1; 0.11 package 3 contract section 4).
struct HubPluginInfo {
  QString id;
  QString version;
  QString name;
  QString description;
  int api = 1;
  QString origin;  // as the Hub classified it: "official" | "unverified" (the Player checks the signature itself)
  QString sha256;  // lowercase hex of the .fbplugin file: the pin of the download
  qint64 size = 0;
  QStringList permissions;
  QJsonObject requires_;
  QStringList provides;
  QJsonArray games;
  QString createdAt;
};
// nullopt for an entry with an invalid id, version, hash or origin.
std::optional<HubPluginInfo> parseHubPluginInfo(const QJsonObject& o);

struct PluginDownloadResult {
  bool ok = false;
  QString id;
  QString version;
  QString sha256;    // the pin that was asked for
  QByteArray bytes;  // the file, only when ok (hash checked against the pin)
  QString problem;   // not_supported | not_on_hub | invalid_hash | invalid_size | too_large | download_failed
  QString detail;
};

// Plugin list and file download of the active Hub. Verifies the SHA-256 pin of a download; installing (zip checks,
// signature) is the PluginManager's job. Without plugins_v1 (older Hub) nothing is requested.
class PluginHubClient : public QObject {
  Q_OBJECT
 public:
  enum class State { Idle, Loading, Ready, Failed };

  explicit PluginHubClient(HubConnection* connection, QObject* parent = nullptr);

  bool supported() const;  // connected and the Hub advertises plugins_v1
  void refresh();          // GET /plugins
  State state() const { return state_; }
  const QList<HubPluginInfo>& plugins() const { return plugins_; }
  QString errorMessage() const { return error_; }

  // GET /plugins/<id>/<version>/file; the body must hash to `sha256`. expectedSize 0 = unknown.
  void download(const QString& pluginId, const QString& version, const QString& sha256, qint64 expectedSize = 0);

 signals:
  void stateChanged();
  void downloadFinished(const framebeam::PluginDownloadResult& result);

 private:
  QPointer<HubConnection> conn_;
  QList<HubPluginInfo> plugins_;
  State state_ = State::Idle;
  QString error_;
  quint64 generation_ = 0;
};

}  // namespace framebeam

Q_DECLARE_METATYPE(framebeam::PluginDownloadResult)
