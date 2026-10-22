#include "localromindex.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QtConcurrent>

#include <memory>

namespace uzume {

QString LocalRomFile::filename() const { return QFileInfo(path).fileName(); }

// ---------------------------------------------------------------- RomHashCache

RomHashCache::RomHashCache(const QString& filePath) : file_(filePath) {
  if (file_.isEmpty()) return;
  QFile f(file_);
  if (!f.open(QIODevice::ReadOnly)) return;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  for (const QJsonValue& v : doc.object().value(QStringLiteral("files")).toArray()) {
    const QJsonObject o = v.toObject();
    LocalRomFile e;
    e.path = o.value(QStringLiteral("path")).toString();
    e.size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble(-1));
    e.mtimeMs = static_cast<qint64>(o.value(QStringLiteral("mtime_ms")).toDouble(0));
    e.sha256 = o.value(QStringLiteral("sha256")).toString();
    e.system = o.value(QStringLiteral("system")).toString();
    e.gameCode = o.value(QStringLiteral("game_code")).toString();
    e.gameVersion = o.value(QStringLiteral("game_version")).toInt(-1);
    if (!e.path.isEmpty() && e.size >= 0 && e.sha256.size() == 64) entries_.insert(e.path, e);
  }
}

std::optional<LocalRomFile> RomHashCache::lookup(const QString& path, qint64 size, qint64 mtimeMs) const {
  const auto it = entries_.constFind(path);
  if (it == entries_.constEnd() || it->size != size || it->mtimeMs != mtimeMs) return std::nullopt;
  return *it;
}

void RomHashCache::put(const LocalRomFile& f) { entries_.insert(f.path, f); }

void RomHashCache::retainOnly(const QSet<QString>& paths) {
  for (auto it = entries_.begin(); it != entries_.end();) {
    it = paths.contains(it.key()) ? std::next(it) : entries_.erase(it);
  }
}

bool RomHashCache::save() const {
  if (file_.isEmpty()) return false;
  QJsonArray arr;
  for (const LocalRomFile& e : entries_) {
    arr.append(QJsonObject{{QStringLiteral("path"), e.path},
                           {QStringLiteral("size"), static_cast<double>(e.size)},
                           {QStringLiteral("mtime_ms"), static_cast<double>(e.mtimeMs)},
                           {QStringLiteral("sha256"), e.sha256},
                           {QStringLiteral("system"), e.system},
                           {QStringLiteral("game_code"), e.gameCode},
                           {QStringLiteral("game_version"), e.gameVersion}});
  }
  QDir().mkpath(QFileInfo(file_).absolutePath());
  QSaveFile f(file_);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("files"), arr}}).toJson(QJsonDocument::Compact));
  return f.commit();
}

// ---------------------------------------------------------------- scan

namespace {
bool hashFile(const QString& path, const std::atomic_bool* cancel, QString* hex) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return false;
  QCryptographicHash h(QCryptographicHash::Sha256);
  QByteArray buf(4 << 20, Qt::Uninitialized);
  while (!f.atEnd()) {
    if (cancel && cancel->load()) return false;
    const qint64 n = f.read(buf.data(), buf.size());
    if (n < 0) return false;
    h.addData(QByteArrayView(buf.constData(), n));
  }
  *hex = QString::fromLatin1(h.result().toHex());
  return true;
}
}  // namespace

LocalScanResult scanLibraryFolders(const LocalScanOptions& opt, RomHashCache& cache) {
  LocalScanResult res;
  struct Candidate {
    QString path, system;
    qint64 size, mtimeMs;
  };
  QList<Candidate> cands;
  QSet<QString> seen;
  for (const QString& folder : opt.folders) {
    QDirIterator it(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      if (opt.cancel && opt.cancel->load()) {
        res.cancelled = true;
        return res;
      }
      const QString path = it.next();
      const QFileInfo fi(path);
      const QString ext = QStringLiteral(".") + fi.suffix().toLower();
      const auto sys = opt.extensionToSystem.constFind(ext);
      if (sys == opt.extensionToSystem.constEnd() || fi.size() <= 0) continue;
      const QString canon = fi.canonicalFilePath();
      if (canon.isEmpty() || seen.contains(canon)) continue;  // overlapping folders, symlinks
      seen.insert(canon);
      cands.append({fi.absoluteFilePath(), *sys, fi.size(), fi.lastModified().toMSecsSinceEpoch()});
    }
  }
  const int total = static_cast<int>(cands.size());
  int done = 0;
  QSet<QString> paths;
  for (const Candidate& c : std::as_const(cands)) {
    if (opt.cancel && opt.cancel->load()) {
      res.cancelled = true;
      break;
    }
    paths.insert(c.path);
    if (auto hit = cache.lookup(c.path, c.size, c.mtimeMs); hit && hit->system == c.system) {
      res.files.append(*hit);
    } else {
      LocalRomFile f;
      f.path = c.path;
      f.size = c.size;
      f.mtimeMs = c.mtimeMs;
      f.system = c.system;
      if (!hashFile(c.path, opt.cancel, &f.sha256)) {
        if (opt.cancel && opt.cancel->load()) {
          res.cancelled = true;
          break;
        }
        ++res.unreadable;
        ++done;
        continue;
      }
      ++res.hashed;
      if (opt.identity) {
        const LocalRomIdentity id = opt.identity(c.path, c.system);
        f.gameCode = id.code;
        f.gameVersion = id.code.isEmpty() ? -1 : id.version;
      }
      cache.put(f);
      res.files.append(f);
    }
    ++done;
    if (opt.progress) opt.progress(done, total);
  }
  if (!res.cancelled) cache.retainOnly(paths);
  return res;
}

// ---------------------------------------------------------------- LocalRomIndex

LocalRomIndex::LocalRomIndex(const QString& cacheFilePath, QObject* parent) : QObject(parent), cacheFile_(cacheFilePath) {
  // Start with the cached view so that launching works before the first scan finished (verified per file by pathForSha).
  files_ = RomHashCache(cacheFile_).all();
  reindex();
}

void LocalRomIndex::reindex() {
  bySha_.clear();
  bySha_.reserve(files_.size());
  for (int i = 0; i < files_.size(); ++i) {
    bySha_[files_.at(i).sha256].append(i);
  }
}

LocalRomIndex::~LocalRomIndex() { *cancel_ = true; }

bool LocalRomIndex::rescan(const LocalScanOptions& options) {
  if (scanning_) return false;
  scanning_ = true;
  cancel_ = std::make_shared<std::atomic_bool>(false);  // a new flag per scan: an older cancelled task never sees the reset
  const quint64 gen = ++generation_;
  emit scanStarted();
  LocalScanOptions opt = options;
  opt.cancel = cancel_.get();
  QPointer<LocalRomIndex> self(this);
  opt.progress = [self](int done, int total) {
    if (self) QMetaObject::invokeMethod(self.data(), [self, done, total]() { if (self) emit self->scanProgress(done, total); }, Qt::QueuedConnection);
  };
  const QString cacheFile = cacheFile_;
  auto* watcher = new QFutureWatcher<LocalScanResult>(this);
  connect(watcher, &QFutureWatcher<LocalScanResult>::finished, this, [this, watcher, gen]() {
    LocalScanResult r = watcher->result();
    watcher->deleteLater();
    scanning_ = false;
    if (gen != generation_) return;
    if (!r.cancelled) {
      files_ = r.files;
      reindex();
    }
    emit scanFinished(static_cast<int>(files_.size()), r.hashed, r.cancelled);
  });
  const std::shared_ptr<std::atomic_bool> keepFlag = cancel_;  // keeps opt.cancel valid for the whole task
  watcher->setFuture(QtConcurrent::run([opt, cacheFile, keepFlag]() {
    RomHashCache cache(cacheFile);
    LocalScanResult r = scanLibraryFolders(opt, cache);
    if (!r.cancelled) cache.save();
    return r;
  }));
  return true;
}

void LocalRomIndex::cancelScan() { *cancel_ = true; }

QString LocalRomIndex::pathForSha(const QString& sha256) {
  const auto it = bySha_.constFind(sha256);
  if (it == bySha_.cend()) return {};
  for (const int i : it.value()) {
    const LocalRomFile& f = files_.at(i);
    const QFileInfo fi(f.path);
    if (fi.isFile() && fi.size() == f.size && fi.lastModified().toMSecsSinceEpoch() == f.mtimeMs) return f.path;
  }
  return {};
}

QList<LocalRomFile> LocalRomIndex::filesFor(const QString& sha256) const {
  QList<LocalRomFile> out;
  const auto it = bySha_.constFind(sha256);
  if (it == bySha_.cend()) return out;
  for (const int i : it.value()) out.append(files_.at(i));
  return out;
}

void LocalRomIndex::clear() {
  files_.clear();
  bySha_.clear();
  QFile::remove(cacheFile_);
}

}  // namespace uzume
