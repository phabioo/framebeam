#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <optional>

#include "hubconnection.h"
#include "hubprotocol.h"

namespace framebeam {

// Library des aktiven Hubs (GET /games). Wird beim Trennen geleert; nie hubuebergreifend zusammengefuehrt.
class HubLibrary : public QObject {
  Q_OBJECT
 public:
  explicit HubLibrary(HubConnection* connection, QObject* parent = nullptr);

  void reload();
  bool isLoading() const { return loading_; }
  const QList<GameEntry>& games() const { return games_; }
  std::optional<GameEntry> gameByRomSha(const QString& sha256) const;
  int skippedEntries() const { return skipped_; }  // ungueltige Hub-Eintraege

 signals:
  void loaded();
  void loadFailed(const QString& code, const QString& message);
  void cleared();

 private:
  QPointer<HubConnection> conn_;
  QList<GameEntry> games_;
  bool loading_ = false;
  int skipped_ = 0;
  quint64 generation_ = 0;
};

}  // namespace framebeam
