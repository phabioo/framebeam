#include "librarymodel.h"

#include <QDateTime>
#include <QLocale>
#include <algorithm>

namespace framebeam::ui {

LibraryModel::LibraryModel(QObject* parent) : QAbstractListModel(parent) {
  collator_.setCaseSensitivity(Qt::CaseInsensitive);
}

// Natural, case-insensitive title order ("Orbit 2" before "Orbit 10"): digit runs compare by value, the rest through the
// collator (QCollator::setNumericMode is not available on every Qt build, so the digit runs are handled here).
int LibraryModel::compareTitles(const QString& a, const QString& b) const {
  qsizetype i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (a.at(i).isDigit() && b.at(j).isDigit()) {
      qsizetype ie = i, je = j;
      while (ie < a.size() && a.at(ie).isDigit()) ++ie;
      while (je < b.size() && b.at(je).isDigit()) ++je;
      QStringView da = QStringView(a).mid(i, ie - i), db = QStringView(b).mid(j, je - j);
      while (da.size() > 1 && da.front() == QLatin1Char('0')) da = da.mid(1);
      while (db.size() > 1 && db.front() == QLatin1Char('0')) db = db.mid(1);
      if (da.size() != db.size()) return da.size() < db.size() ? -1 : 1;
      if (const int c = da.compare(db)) return c < 0 ? -1 : 1;
      i = ie;
      j = je;
      continue;
    }
    qsizetype ie = i, je = j;
    while (ie < a.size() && !a.at(ie).isDigit()) ++ie;
    while (je < b.size() && !b.at(je).isDigit()) ++je;
    if (const int c = collator_.compare(QStringView(a).mid(i, ie - i), QStringView(b).mid(j, je - j))) return c < 0 ? -1 : 1;
    i = ie;
    j = je;
  }
  if (i < a.size()) return 1;
  if (j < b.size()) return -1;
  return 0;
}

int LibraryModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

QHash<int, QByteArray> LibraryModel::roleNames() const {
  return {{GameIdRole, "gameId"},       {TitleRole, "title"},         {SystemRole, "system"},
          {MonogramRole, "monogram"},   {RomShaRole, "romSha256"},    {RomSizeRole, "romSize"},
          {StateKindRole, "stateKind"}, {StatusTextRole, "statusText"}, {StatusToneRole, "statusTone"},
          {ProgressRole, "progress"}, {SyncKindRole, "syncKind"}, {SyncTextRole, "syncText"},
          {TileTextRole, "tileText"}, {TileToneRole, "tileTone"}, {NeedsAttentionRole, "needsAttention"},
          {ReadyGroupRole, "readyGroup"}, {AddedTextRole, "addedText"}};
}

QString LibraryModel::formatSize(qint64 bytes) {
  constexpr qint64 kMiB = 1024 * 1024;
  if (bytes >= kMiB) {
    return QStringLiteral("%1 MB").arg(QLocale::c().toString(static_cast<double>(bytes) / kMiB, 'f', bytes >= 10 * kMiB ? 0 : 1));
  }
  return QStringLiteral("%1 KB").arg(qMax<qint64>(1, (bytes + 1023) / 1024));
}

QString LibraryModel::monogram(const QString& title) {
  QString out;
  const QStringList words = title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  for (const QString& w : words) {
    for (const QChar c : w) {
      if (c.isLetterOrNumber()) {
        out.append(c.toUpper());
        break;
      }
    }
    if (out.size() >= 2) {
      break;
    }
  }
  return out.isEmpty() ? QStringLiteral("?") : out;
}

QString LibraryModel::stateKind(RomState s) {
  switch (s) {
    case RomState::Ready: return QStringLiteral("ready");
    case RomState::DownloadNeeded: return QStringLiteral("download");
    case RomState::Downloading: return QStringLiteral("downloading");
    case RomState::HashMismatch: return QStringLiteral("mismatch");
    case RomState::Failed: return QStringLiteral("failed");
    default: break;
  }
  // Validating and future states: treat as neutral ("Verifying…").
  return s == RomState::Validating ? QStringLiteral("validating") : QStringLiteral("unknown");
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= visible_.size()) {
    return {};
  }
  const Item& it = items_.at(visible_.at(index.row()));
  const QString kind = stateKind(it.status.state);
  switch (role) {
    case GameIdRole: return it.game.id;
    case TitleRole: return it.game.title;
    case SystemRole: return it.game.system.toUpper();
    case MonogramRole: return monogram(it.game.title);
    case RomShaRole: return it.game.romSha256;
    case RomSizeRole: return it.game.romSize;
    case StateKindRole: return kind;
    case SyncKindRole: return it.sync;
    case SyncTextRole: return syncText(it.sync);
    case NeedsAttentionRole: return categoryOf(it) == Category::Attention;
    case ReadyGroupRole: return inReadyGroup(it);
    case AddedTextRole:
      return it.addedMs > 0 ? QVariant(QDateTime::fromMSecsSinceEpoch(it.addedMs).toLocalTime().toString(QStringLiteral("dd.MM.yyyy"))) : QVariant(QString());
    case TileToneRole:
    case TileTextRole: {
      QString text;
      QString tone = QStringLiteral("neutral");
      const qint64 total = it.status.totalBytes > 0 ? it.status.totalBytes : it.game.romSize;
      if (kind == QLatin1String("mismatch")) {
        text = tr("✕ Hash mismatch · reload");
        tone = QStringLiteral("error");
      } else if (it.sync == QLatin1String("conflict")) {
        text = tr("▲ Save conflict");
        tone = QStringLiteral("warn");
      } else if (!it.attention.isEmpty()) {
        text = QStringLiteral("▲ ") + it.attention;
        tone = QStringLiteral("warn");
      } else if (kind == QLatin1String("failed")) {
        text = tr("✕ Download failed · retry");
        tone = QStringLiteral("error");
      } else if (kind == QLatin1String("download")) {
        text = tr("↓ Download required · %1").arg(formatSize(total));
      } else if (kind == QLatin1String("downloading")) {
        const int pct = total > 0 ? static_cast<int>(it.status.receivedBytes * 100 / total) : 0;
        text = tr("↓ Downloading %1 %").arg(qBound(0, pct, 100));
      } else if (kind != QLatin1String("ready")) {
        text = tr("Verifying…");
      } else if (it.sync == QLatin1String("pending")) {
        text = tr("⟳ Save sync pending");
      } else {
        text = tr("● Ready");
        tone = QStringLiteral("ok");
      }
      return role == TileTextRole ? QVariant(text) : QVariant(tone);
    }
    case ProgressRole: {
      const qint64 total = it.status.totalBytes > 0 ? it.status.totalBytes : it.game.romSize;
      return total > 0 ? static_cast<double>(it.status.receivedBytes) / static_cast<double>(total) : 0.0;
    }
    case StatusToneRole:
      if (kind == QLatin1String("ready")) return QStringLiteral("ok");
      if (kind == QLatin1String("mismatch") || kind == QLatin1String("failed")) return QStringLiteral("error");
      return QStringLiteral("neutral");
    case StatusTextRole: {
      if (kind == QLatin1String("ready")) return tr("Ready");
      if (kind == QLatin1String("download")) {
        const qint64 total = it.status.totalBytes > 0 ? it.status.totalBytes : it.game.romSize;
        return tr("Download needed · %1").arg(formatSize(total));
      }
      if (kind == QLatin1String("downloading")) {
        const qint64 total = it.status.totalBytes > 0 ? it.status.totalBytes : it.game.romSize;
        const int pct = total > 0 ? static_cast<int>(it.status.receivedBytes * 100 / total) : 0;
        return tr("Downloading %1 %").arg(qBound(0, pct, 100));
      }
      if (kind == QLatin1String("mismatch")) return tr("Hash mismatch · reload");
      if (kind == QLatin1String("failed")) return tr("Download failed · retry");
      return tr("Verifying…");
    }
    default: return {};
  }
}

QString LibraryModel::attentionOf(const Item& it) {
  if (it.status.state == RomState::HashMismatch) return tr("Hash mismatch");
  if (it.sync == QLatin1String("conflict")) return tr("Save conflict");
  return it.attention;
}

LibraryModel::Category LibraryModel::categoryOf(const Item& it) {
  if (!attentionOf(it).isEmpty()) return Category::Attention;
  return it.status.state == RomState::Ready ? Category::Ready : Category::Download;
}

QVariantMap LibraryModel::countsFor(bool applyText) const {
  int all = 0, ready = 0, attention = 0, download = 0;
  for (const Item& it : items_) {
    if (applyText && !matchesText(it)) {
      continue;
    }
    ++all;
    switch (categoryOf(it)) {
      case Category::Ready: ++ready; break;
      case Category::Attention: ++attention; break;
      case Category::Download: ++download; break;
    }
  }
  return {{QStringLiteral("all"), all},
          {QStringLiteral("ready"), ready},
          {QStringLiteral("attention"), attention},
          {QStringLiteral("download"), download}};
}

QString LibraryModel::attentionText(const QString& gameId) const {
  for (const Item& it : items_) {
    if (it.game.id == gameId) return attentionOf(it);
  }
  return {};
}

bool LibraryModel::matchesText(const Item& it) const {
  const QString needle = filterText_.trimmed();
  return needle.isEmpty() || it.game.title.contains(needle, Qt::CaseInsensitive);
}

bool LibraryModel::matches(const Item& it) const {
  if (filter_ != QLatin1String("all")) {
    const Category c = categoryOf(it);
    if ((filter_ == QLatin1String("ready") && c != Category::Ready) || (filter_ == QLatin1String("attention") && c != Category::Attention) ||
        (filter_ == QLatin1String("download") && c != Category::Download)) {
      return false;
    }
  }
  return matchesText(it);
}

// "Save sync pending" does not count as ready (decision aa).
bool LibraryModel::inReadyGroup(const Item& it) const {
  return categoryOf(it) == Category::Ready && it.sync != QLatin1String("pending");
}

bool LibraryModel::isReady(const QString& gameId) const {
  for (const Item& it : items_) {
    if (it.game.id == gameId) return inReadyGroup(it);
  }
  return false;
}

qint64 LibraryModel::parseAdded(const QString& iso) {
  const QDateTime t = QDateTime::fromString(iso, Qt::ISODate);
  return t.isValid() ? t.toMSecsSinceEpoch() : 0;
}

QStringList LibraryModel::sortKeys() {
  return {QStringLiteral("name_asc"),  QStringLiteral("name_desc"), QStringLiteral("added_desc"), QStringLiteral("added_asc"),
          QStringLiteral("size_desc"), QStringLiteral("size_asc"),  QStringLiteral("system"),     QStringLiteral("played")};
}

bool LibraryModel::lessThan(const Item& a, const Item& b) const {
  const auto byName = [this](const Item& x, const Item& y) {
    const int c = compareTitles(x.game.title, y.game.title);
    return c != 0 ? c : QString::compare(x.game.id, y.game.id);
  };
  const auto key = [this](const QString& id) { return lastPlayed_.value(id, 0); };
  if (sortKey_ == QLatin1String("name_desc")) {
    return byName(a, b) > 0;
  }
  if (sortKey_ == QLatin1String("added_desc") || sortKey_ == QLatin1String("added_asc")) {
    if (a.addedMs != b.addedMs) {
      return sortKey_ == QLatin1String("added_desc") ? a.addedMs > b.addedMs : a.addedMs < b.addedMs;
    }
    return byName(a, b) < 0;
  }
  if (sortKey_ == QLatin1String("size_desc") || sortKey_ == QLatin1String("size_asc")) {
    if (a.game.romSize != b.game.romSize) {
      return sortKey_ == QLatin1String("size_desc") ? a.game.romSize > b.game.romSize : a.game.romSize < b.game.romSize;
    }
    return byName(a, b) < 0;
  }
  if (sortKey_ == QLatin1String("system")) {
    const int c = QString::compare(a.game.system, b.game.system, Qt::CaseInsensitive);
    return c != 0 ? c < 0 : byName(a, b) < 0;
  }
  if (sortKey_ == QLatin1String("played")) {
    const qint64 pa = key(a.game.id), pb = key(b.game.id);
    if (pa != pb) {
      return pa > pb;  // never played (0) comes last
    }
    return byName(a, b) < 0;
  }
  return byName(a, b) < 0;
}

void LibraryModel::sortVisible(QList<int>& list) const {
  const bool groups = readyFirst_ && readyFirstApplicable();
  std::stable_sort(list.begin(), list.end(), [this, groups](int x, int y) {
    const Item& a = items_.at(x);
    const Item& b = items_.at(y);
    if (groups) {
      const bool ra = inReadyGroup(a), rb = inReadyGroup(b);
      if (ra != rb) {
        return ra;
      }
    }
    return lessThan(a, b);
  });
}

int LibraryModel::readyCount() const {
  int n = 0;
  for (const Item& it : items_) {
    n += it.status.state == RomState::Ready ? 1 : 0;
  }
  return n;
}

void LibraryModel::rebuild() {
  QList<int> next;
  for (int i = 0; i < items_.size(); ++i) {
    if (matches(items_.at(i))) {
      next.append(i);
    }
  }
  sortVisible(next);
  int ready = 0;
  for (const int i : std::as_const(next)) {
    ready += inReadyGroup(items_.at(i)) ? 1 : 0;
  }
  readyGroupCount_ = ready;
  notReadyGroupCount_ = static_cast<int>(next.size()) - ready;
  if (next == visible_) {
    return;
  }
  beginResetModel();
  visible_ = next;
  endResetModel();
  emit countChanged();
}

void LibraryModel::setSortKey(const QString& key) {
  if (!sortKeys().contains(key) || key == sortKey_) {
    return;
  }
  sortKey_ = key;
  rebuild();
  emit sortChanged();
  emit countChanged();
}

void LibraryModel::setReadyFirst(bool on) {
  if (on == readyFirst_) {
    return;
  }
  readyFirst_ = on;
  rebuild();
  emit sortChanged();
  emit countChanged();
}

void LibraryModel::setLastPlayed(const QHash<QString, qint64>& byGameId) {
  lastPlayed_ = byGameId;
  if (sortKey_ == QLatin1String("played")) {
    rebuild();
    emit countChanged();
  }
}

void LibraryModel::noteLastPlayed(const QString& gameId, qint64 msecs) {
  lastPlayed_.insert(gameId, msecs);
  if (sortKey_ == QLatin1String("played")) {
    rebuild();
    emit countChanged();
  }
}

void LibraryModel::setSystemLabel(const QString& label) {
  if (label != systemLabel_) {
    systemLabel_ = label;
    emit countChanged();
  }
}

QString LibraryModel::countText() const {
  const int total = totalCount();
  const int shown = rowCount();
  const bool text = !filterText_.trimmed().isEmpty();
  if (filter_ == QLatin1String("all")) {
    if (!text) {
      const QString base = total == 1 ? tr("1 game") : tr("%1 games").arg(total);
      return systemLabel_.isEmpty() ? base : tr("%1 · %2").arg(base, systemLabel_);
    }
    return tr("%1 of %2 games").arg(shown).arg(total);
  }
  if (!text) {
    return tr("%1 of %2 games").arg(shown).arg(total);
  }
  const int inChip = countsFor(false).value(filter_).toInt();
  const QString what = filter_ == QLatin1String("ready")       ? tr("ready games")
                       : filter_ == QLatin1String("attention") ? tr("games needing attention")
                                                               : tr("games not downloaded");
  return tr("%1 of %2 %3").arg(shown).arg(inChip).arg(what);
}

QVariantMap LibraryModel::outsideFilter() const {
  if (!visible_.isEmpty() || filter_ == QLatin1String("all") || filterText_.trimmed().isEmpty()) {
    return {};
  }
  int n = 0;
  const Item* first = nullptr;
  for (const Item& it : items_) {
    if (matchesText(it) && !matches(it)) {
      ++n;
      if (first == nullptr) first = &it;
    }
  }
  if (first == nullptr) {
    return {};
  }
  QString reason;
  switch (categoryOf(*first)) {
    case Category::Attention: {
      QString why = attentionOf(*first);
      if (!why.isEmpty()) why[0] = why.at(0).toLower();
      reason = tr("needs attention · %1").arg(why);
      break;
    }
    case Category::Download: reason = tr("not downloaded"); break;
    case Category::Ready: reason = tr("ready"); break;
  }
  return {{QStringLiteral("count"), n}, {QStringLiteral("title"), first->game.title}, {QStringLiteral("reason"), reason}};
}

void LibraryModel::setFilterText(const QString& t) {
  if (filterText_ == t) {
    return;
  }
  filterText_ = t;
  rebuild();
  emit filterChanged();
  emit countChanged();  // chip counts and the toolbar count follow the search
}

void LibraryModel::setReadyOnly(bool on) { setFilter(on ? QStringLiteral("ready") : QStringLiteral("all")); }

void LibraryModel::setFilter(const QString& filter) {
  static const QStringList known = {QStringLiteral("all"), QStringLiteral("ready"), QStringLiteral("attention"), QStringLiteral("download")};
  const QString f = known.contains(filter) ? filter : QStringLiteral("all");
  if (filter_ == f) {
    return;
  }
  filter_ = f;
  rebuild();
  emit filterChanged();
  emit countChanged();
}

void LibraryModel::setAttention(const QHash<QString, QString>& byGameId) {
  bool anyChange = false;
  for (Item& it : items_) {
    const QString next = byGameId.value(it.game.id);
    if (it.attention != next) {
      it.attention = next;
      anyChange = true;
    }
  }
  if (!anyChange) return;
  const QList<int> before = visible_;
  rebuild();  // resets when the visible set changed (filter "attention" / "ready")
  if (before == visible_ && !visible_.isEmpty()) {
    emit dataChanged(index(0), index(static_cast<int>(visible_.size()) - 1));
  }
  emit countChanged();
}

void LibraryModel::setGames(const QList<GameEntry>& games, const std::function<RomStatus(const GameEntry&)>& statusOf) {
  // Same games in the same order (periodic refresh): update in place, no reset (scroll position and selection stay).
  if (games.size() == items_.size() && !games.isEmpty()) {
    bool same = true;
    for (int i = 0; i < games.size() && same; ++i) {
      same = games.at(i).id == items_.at(i).game.id;
    }
    if (same) {
      for (int i = 0; i < games.size(); ++i) {
        items_[i].game = games.at(i);
        items_[i].addedMs = parseAdded(games.at(i).addedAt);
        items_[i].status = statusOf ? statusOf(games.at(i)) : RomStatus{};
      }
      const QList<int> before = visible_;
      rebuild();
      if (before == visible_ && !visible_.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(visible_.size()) - 1));
      }
      emit countChanged();
      return;
    }
  }
  beginResetModel();
  items_.clear();
  for (const GameEntry& g : games) {
    items_.append({g, statusOf ? statusOf(g) : RomStatus{}});
    items_.last().addedMs = parseAdded(g.addedAt);
  }
  visible_.clear();
  for (int i = 0; i < items_.size(); ++i) {
    if (matches(items_.at(i))) {
      visible_.append(i);
    }
  }
  sortVisible(visible_);
  int ready = 0;
  for (const int i : std::as_const(visible_)) {
    ready += inReadyGroup(items_.at(i)) ? 1 : 0;
  }
  readyGroupCount_ = ready;
  notReadyGroupCount_ = static_cast<int>(visible_.size()) - ready;
  endResetModel();
  emit countChanged();
}

void LibraryModel::clear() { setGames({}, {}); }

void LibraryModel::setStatus(const QString& romSha256, const RomStatus& status) {
  bool any = false;
  bool progressOnly = status.state == RomState::Downloading;
  for (int i = 0; i < items_.size(); ++i) {
    if (items_.at(i).game.romSha256 == romSha256) {
      progressOnly = progressOnly && items_.at(i).status.state == RomState::Downloading;
      items_[i].status = status;
      any = true;
    }
  }
  if (!any) {
    return;
  }
  if (progressOnly) {
    // Only the byte counters moved: filter, order and group counts cannot change, so skip the rebuild and notify the
    // roles that show progress.
    for (int row = 0; row < visible_.size(); ++row) {
      if (items_.at(visible_.at(row)).game.romSha256 == romSha256) {
        emit dataChanged(index(row), index(row), {ProgressRole, StatusTextRole, TileTextRole});
      }
    }
    return;
  }
  const QList<int> before = visible_;
  rebuild();  // filter chips and Ready first depend on the status
  if (before == visible_) {
    for (int row = 0; row < visible_.size(); ++row) {
      if (items_.at(visible_.at(row)).game.romSha256 == romSha256) {
        emit dataChanged(index(row), index(row));
      }
    }
  }
  emit countChanged();
}

QString LibraryModel::syncText(const QString& kind) {
  if (kind == QLatin1String("synced")) return tr("Synced");
  if (kind == QLatin1String("pending")) return tr("Sync pending");
  if (kind == QLatin1String("conflict")) return tr("Conflict");
  return {};
}

QString LibraryModel::syncKind(const QString& gameId) const {
  for (const Item& it : items_) {
    if (it.game.id == gameId) {
      return it.sync;
    }
  }
  return QStringLiteral("none");
}

void LibraryModel::setSyncKind(const QString& gameId, const QString& kind) {
  for (int i = 0; i < items_.size(); ++i) {
    if (items_.at(i).game.id != gameId || items_.at(i).sync == kind) {
      continue;
    }
    items_[i].sync = kind;
    const QList<int> before = visible_;
    rebuild();  // a conflict moves the game between the chips, "sync pending" out of the ready group
    if (before == visible_) {
      const qsizetype row = visible_.indexOf(i);
      if (row >= 0) {
        emit dataChanged(index(static_cast<int>(row)), index(static_cast<int>(row)));
      }
    }
    emit countChanged();
  }
}

void LibraryModel::setSyncKinds(const QHash<QString, QString>& kinds) {
  bool changed = false;
  for (Item& it : items_) {
    const auto k = kinds.constFind(it.game.id);
    if (k != kinds.cend() && it.sync != k.value()) {
      it.sync = k.value();
      changed = true;
    }
  }
  if (!changed) {
    return;
  }
  const QList<int> before = visible_;
  rebuild();
  if (before == visible_ && !visible_.isEmpty()) {
    emit dataChanged(index(0), index(static_cast<int>(visible_.size()) - 1));
  }
  emit countChanged();
}

int LibraryModel::rowOfGame(const QString& gameId) const {
  for (int row = 0; row < visible_.size(); ++row) {
    if (items_.at(visible_.at(row)).game.id == gameId) {
      return row;
    }
  }
  return -1;
}

std::optional<GameEntry> LibraryModel::game(const QString& gameId) const {
  for (const Item& it : items_) {
    if (it.game.id == gameId) {
      return it.game;
    }
  }
  return std::nullopt;
}

std::optional<RomStatus> LibraryModel::status(const QString& gameId) const {
  for (const Item& it : items_) {
    if (it.game.id == gameId) {
      return it.status;
    }
  }
  return std::nullopt;
}

void LibraryGroupModel::setLibrary(QObject* library) {
  if (library == library_) {
    return;
  }
  library_ = library;
  setSourceModel(qobject_cast<QAbstractItemModel*>(library));
  setFilterRole(LibraryModel::ReadyGroupRole);
  emit libraryChanged();
}

void LibraryGroupModel::setGroup(int group) {
  if (group == group_) {
    return;
  }
  group_ = group;
  invalidateFilter();
  emit groupChanged();
}

bool LibraryGroupModel::filterAcceptsRow(int row, const QModelIndex& parent) const {
  if (group_ < 0 || sourceModel() == nullptr) {
    return true;
  }
  const bool ready = sourceModel()->data(sourceModel()->index(row, 0, parent), LibraryModel::ReadyGroupRole).toBool();
  return group_ == 0 ? ready : !ready;
}

}  // namespace framebeam::ui
