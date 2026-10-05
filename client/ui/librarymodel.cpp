#include "librarymodel.h"

#include <QLocale>

namespace framebeam::ui {

LibraryModel::LibraryModel(QObject* parent) : QAbstractListModel(parent) {}

int LibraryModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

QHash<int, QByteArray> LibraryModel::roleNames() const {
  return {{GameIdRole, "gameId"},       {TitleRole, "title"},         {SystemRole, "system"},
          {MonogramRole, "monogram"},   {RomShaRole, "romSha256"},    {RomSizeRole, "romSize"},
          {StateKindRole, "stateKind"}, {StatusTextRole, "statusText"}, {StatusToneRole, "statusTone"},
          {ProgressRole, "progress"}, {SyncKindRole, "syncKind"}, {SyncTextRole, "syncText"}};
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

bool LibraryModel::matches(const Item& it) const {
  if (readyOnly_ && it.status.state != RomState::Ready) {
    return false;
  }
  const QString needle = filterText_.trimmed();
  return needle.isEmpty() || it.game.title.contains(needle, Qt::CaseInsensitive);
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
  if (next == visible_) {
    return;
  }
  beginResetModel();
  visible_ = next;
  endResetModel();
  emit countChanged();
}

void LibraryModel::setFilterText(const QString& t) {
  if (filterText_ == t) {
    return;
  }
  filterText_ = t;
  rebuild();
  emit filterChanged();
}

void LibraryModel::setReadyOnly(bool on) {
  if (readyOnly_ == on) {
    return;
  }
  readyOnly_ = on;
  rebuild();
  emit filterChanged();
}

void LibraryModel::setGames(const QList<GameEntry>& games, const std::function<RomStatus(const GameEntry&)>& statusOf) {
  beginResetModel();
  items_.clear();
  for (const GameEntry& g : games) {
    items_.append({g, statusOf ? statusOf(g) : RomStatus{}});
  }
  visible_.clear();
  for (int i = 0; i < items_.size(); ++i) {
    if (matches(items_.at(i))) {
      visible_.append(i);
    }
  }
  endResetModel();
  emit countChanged();
}

void LibraryModel::clear() { setGames({}, {}); }

void LibraryModel::setStatus(const QString& romSha256, const RomStatus& status) {
  bool filterAffected = false;
  for (int i = 0; i < items_.size(); ++i) {
    if (items_.at(i).game.romSha256 != romSha256) {
      continue;
    }
    items_[i].status = status;
    const bool wasVisible = visible_.contains(i);
    if (wasVisible != matches(items_.at(i))) {
      filterAffected = true;
    } else if (wasVisible) {
      const int row = static_cast<int>(visible_.indexOf(i));
      emit dataChanged(index(row), index(row));
    }
  }
  if (filterAffected) {
    rebuild();
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
    const qsizetype row = visible_.indexOf(i);
    if (row >= 0) {
      emit dataChanged(index(static_cast<int>(row)), index(static_cast<int>(row)), {SyncKindRole, SyncTextRole});
    }
  }
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

}  // namespace framebeam::ui
