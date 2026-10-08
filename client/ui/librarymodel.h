#pragma once
// LibraryModel: games of the active hub with ROM status, filterable by title and "Ready".

#include <QAbstractListModel>
#include <QCollator>
#include <QSortFilterProxyModel>
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
  // Sort (3c-2): "name_asc" | "name_desc" | "added_desc" | "added_asc" | "size_desc" | "size_asc" | "system" | "played".
  // Persisted per device by the PlayerController (PlayerSettings).
  Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY sortChanged)
  // "Ready first": ready games on top, a divider, then the rest; same sort inside both groups. Not applied (and the toggle
  // disabled) while the "Ready" chip is active: every result is ready then.
  Q_PROPERTY(bool readyFirst READ readyFirst WRITE setReadyFirst NOTIFY sortChanged)
  Q_PROPERTY(bool readyFirstApplicable READ readyFirstApplicable NOTIFY filterChanged)
  // Visible games in the "ready" group / the rest when grouping is on (otherwise all visible games count as ready group).
  Q_PROPERTY(bool grouped READ grouped NOTIFY countChanged)
  Q_PROPERTY(int readyGroupCount READ readyGroupCount NOTIFY countChanged)
  Q_PROPERTY(int notReadyGroupCount READ notReadyGroupCount NOTIFY countChanged)
  // Toolbar count (decision ac): "42 games · Nintendo DS", "37 of 42 games", "0 of 37 ready games".
  Q_PROPERTY(QString countText READ countText NOTIFY countChanged)
  // Empty result: {count, title, reason} of the games that match the search but not the chip; {} otherwise.
  Q_PROPERTY(QVariantMap outsideFilter READ outsideFilter NOTIFY countChanged)
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
    NeedsAttentionRole,  // bool, see attentionOf()
    ReadyGroupRole,      // bool: "ready" for Ready first (ROM ready, no attention, save sync not pending)
    AddedTextRole        // "DD.MM.YYYY" (Hub added_at, local time); empty if unknown
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
  // Chip counts; they follow the search text (not the chip filter), see counts(false) for the totals.
  QVariantMap counts() const { return countsFor(true); }
  QString sortKey() const { return sortKey_; }
  void setSortKey(const QString& key);  // unknown keys are ignored
  static QStringList sortKeys();
  bool readyFirst() const { return readyFirst_; }
  void setReadyFirst(bool on);
  bool readyFirstApplicable() const { return filter_ != QLatin1String("ready"); }
  bool grouped() const { return readyFirst_ && readyFirstApplicable() && readyGroupCount_ > 0 && notReadyGroupCount_ > 0; }
  int readyGroupCount() const { return readyGroupCount_; }
  int notReadyGroupCount() const { return notReadyGroupCount_; }
  QString countText() const;
  QVariantMap outsideFilter() const;
  // Last played on this device (Unix ms per game id of the active Hub); never played = missing.
  void setLastPlayed(const QHash<QString, qint64>& byGameId);
  void noteLastPlayed(const QString& gameId, qint64 msecs);
  // Label for the toolbar count ("Nintendo DS"): set by the PlayerController, empty = none or several systems.
  void setSystemLabel(const QString& label);
  Q_INVOKABLE bool isReady(const QString& gameId) const;
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
  void sortChanged();

 private:
  struct Item {
    GameEntry game;
    RomStatus status;
    QString sync = QStringLiteral("none");
    QString attention;  // core / firmware problem
    qint64 addedMs = 0;
  };
  enum class Category { Ready, Attention, Download };
  static Category categoryOf(const Item& it);
  static QString attentionOf(const Item& it);  // short reason, empty = none
  bool matches(const Item& it) const;
  bool matchesText(const Item& it) const;
  bool inReadyGroup(const Item& it) const;
  bool lessThan(const Item& a, const Item& b) const;
  int compareTitles(const QString& a, const QString& b) const;
  QVariantMap countsFor(bool applyText) const;
  static qint64 parseAdded(const QString& iso);
  void rebuild();
  void sortVisible(QList<int>& list) const;

  QList<Item> items_;
  QList<int> visible_;  // indices into items_
  QString filterText_;
  QString filter_ = QStringLiteral("all");
  QString sortKey_ = QStringLiteral("name_asc");
  bool readyFirst_ = false;
  QHash<QString, qint64> lastPlayed_;
  QString systemLabel_;
  QCollator collator_;
  int readyGroupCount_ = 0;
  int notReadyGroupCount_ = 0;
};

// Filters the LibraryModel to the ready group (group = 0), the not-ready group (group = 1) or everything (group = -1),
// so the Library can draw the two groups as separate grids with a divider between them.
class LibraryGroupModel : public QSortFilterProxyModel {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QObject* library READ library WRITE setLibrary NOTIFY libraryChanged)
  Q_PROPERTY(int group READ group WRITE setGroup NOTIFY groupChanged)
 public:
  explicit LibraryGroupModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}
  QObject* library() const { return library_; }
  void setLibrary(QObject* library);
  int group() const { return group_; }
  void setGroup(int group);
 signals:
  void libraryChanged();
  void groupChanged();
 protected:
  bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
 private:
  QObject* library_ = nullptr;
  int group_ = -1;
};

}  // namespace framebeam::ui
