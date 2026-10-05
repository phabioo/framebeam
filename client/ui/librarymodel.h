#pragma once
// LibraryModel: Spiele des aktiven Hubs mit ROM-Status, filterbar nach Titel und "Bereit".

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <optional>

#include "hubprotocol.h"
#include "romdownloader.h"

namespace framebeam::ui {

class LibraryModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Wird vom PlayerController bereitgestellt")
  Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
  Q_PROPERTY(bool readyOnly READ readyOnly WRITE setReadyOnly NOTIFY filterChanged)
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
  Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
  Q_PROPERTY(int readyCount READ readyCount NOTIFY countChanged)
 public:
  enum Roles {
    GameIdRole = Qt::UserRole + 1,
    TitleRole,
    SystemRole,       // z. B. "NDS"
    MonogramRole,     // bis zu zwei Initialen
    RomShaRole,
    RomSizeRole,
    StateKindRole,    // ready | download | validating | downloading | mismatch | failed | unknown
    StatusTextRole,   // z. B. "Download nötig · 128 MB"
    StatusToneRole,   // ok | neutral | warn | error
    ProgressRole      // 0..1, nur bei downloading sinnvoll
  };
  Q_ENUM(Roles)

  explicit LibraryModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QString filterText() const { return filterText_; }
  void setFilterText(const QString& t);
  bool readyOnly() const { return readyOnly_; }
  void setReadyOnly(bool on);
  int totalCount() const { return static_cast<int>(items_.size()); }
  int readyCount() const;

  void setGames(const QList<GameEntry>& games, const std::function<RomStatus(const GameEntry&)>& statusOf);
  void clear();
  void setStatus(const QString& romSha256, const RomStatus& status);

  Q_INVOKABLE int rowOfGame(const QString& gameId) const;
  std::optional<GameEntry> game(const QString& gameId) const;
  std::optional<RomStatus> status(const QString& gameId) const;

  static QString formatSize(qint64 bytes);
  static QString monogram(const QString& title);
  static QString stateKind(RomState s);

 signals:
  void filterChanged();
  void countChanged();

 private:
  struct Item {
    GameEntry game;
    RomStatus status;
  };
  bool matches(const Item& it) const;
  void rebuild();

  QList<Item> items_;
  QList<int> visible_;  // Indizes in items_
  QString filterText_;
  bool readyOnly_ = false;
};

}  // namespace framebeam::ui
