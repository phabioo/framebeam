#pragma once
// LibraryModel: games of the active hub with ROM status, filterable by title and "Ready".

#include <QAbstractListModel>
#include <QList>
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <optional>

#include "hubprotocol.h"
#include "romdownloader.h"

namespace framebeam::ui {

class LibraryModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")
  Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
  Q_PROPERTY(bool readyOnly READ readyOnly WRITE setReadyOnly NOTIFY filterChanged)
  // Filter chips of the Library (3c): "all" | "ready" | "attention" | "download". Combined with filterText.
  Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
  // Counts of the chips: {all, ready, attention, download}. Every game is in exactly one of the last three
  // (attention wins, then ready), so they add up to all.
  Q_PROPERTY(QVariantMap counts READ counts NOTIFY countChanged)
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
  Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
  Q_PROPERTY(int readyCount READ readyCount NOTIFY countChanged)
 public:
  enum Roles {
    GameIdRole = Qt::UserRole + 1,
    TitleRole,
    SystemRole,       // e.g. "NDS"
    MonogramRole,     // up to two initials
    RomShaRole,
    RomSizeRole,
    StateKindRole,    // ready | download | validating | downloading | mismatch | failed | unknown
    StatusTextRole,   // e.g. "Download needed · 128 MB"
    StatusToneRole,   // ok | neutral | warn | error
    ProgressRole,     // 0..1, only meaningful for downloading
    SyncKindRole,     // none | synced | pending | conflict (save sync)
    SyncTextRole,     // "Synced" | "Sync pending" | "Conflict" | ""
    TileTextRole,     // status line of the tile per 3c, with leading symbol ("● Ready", "▲ Save conflict", ...)
    TileToneRole,     // ok | neutral | warn | error
    NeedsAttentionRole  // bool, see attentionOf()
  };
  Q_ENUM(Roles)

  explicit LibraryModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QString filterText() const { return filterText_; }
  void setFilterText(const QString& t);
  bool readyOnly() const { return filter_ == QLatin1String("ready"); }
  void setReadyOnly(bool on);
  int totalCount() const { return static_cast<int>(items_.size()); }
  int readyCount() const;
  QString filter() const { return filter_; }
  void setFilter(const QString& filter);
  QVariantMap counts() const;
  // "Needs attention" (D14): save conflict, hash mismatch, core missing/incompatible, firmware missing.
  // Core and firmware problems come from the PlayerController (game id -> short text); sync pending is not attention.
  void setAttention(const QHash<QString, QString>& byGameId);
  QString attentionText(const QString& gameId) const;

  void setGames(const QList<GameEntry>& games, const std::function<RomStatus(const GameEntry&)>& statusOf);
  void clear();
  void setStatus(const QString& romSha256, const RomStatus& status);
  void setSyncKind(const QString& gameId, const QString& kind);
  QString syncKind(const QString& gameId) const;
  static QString syncText(const QString& kind);

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
    QString sync = QStringLiteral("none");
    QString attention;  // core / firmware problem
  };
  enum class Category { Ready, Attention, Download };
  static Category categoryOf(const Item& it);
  static QString attentionOf(const Item& it);  // short reason, empty = none
  bool matches(const Item& it) const;
  void rebuild();

  QList<Item> items_;
  QList<int> visible_;  // indices into items_
  QString filterText_;
  QString filter_ = QStringLiteral("all");
};

}  // namespace framebeam::ui
