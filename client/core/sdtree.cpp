#include "sdtree.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cstring>
#include <vector>

#include "savestore.h"

namespace framebeam::sdtree {

namespace {

constexpr int kBlock = 512;
constexpr int kMaxDepth = 40;

bool isHex(const QString& s, int n) {
  if (s.size() != n) {
    return false;
  }
  for (const QChar c : s) {
    const ushort u = c.unicode();
    if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F'))) {
      return false;
    }
  }
  return true;
}

bool validComponent(const QString& c) {
  if (c.isEmpty() || c == QLatin1String(".") || c == QLatin1String("..") || c.contains(QLatin1Char('\\')) || c.contains(QLatin1Char(':')) ||
      c.contains(QChar::ReplacementCharacter) || SaveStore::isWindowsAliasName(c)) {
    return false;
  }
  for (const QChar ch : c) {
    if (ch.unicode() < 0x20 || ch.unicode() == 0x7f) {
      return false;
    }
  }
  return true;
}

// Number of leading components up to and including "data" / "extdata" (8 / 5), or -1 when `rel` is not managed.
int managedAnchor(const QString& rel) {
  const QStringList c = rel.split(QLatin1Char('/'));
  for (const QString& part : c) {
    if (!validComponent(part)) {
      return -1;
    }
  }
  if (c.size() < 6 || c[0] != QLatin1String("sdmc") || c[1] != QLatin1String("Nintendo 3DS") || !isHex(c[2], 32) || !isHex(c[3], 32)) {
    return -1;
  }
  if (c[4] == QLatin1String("extdata")) {
    return 5;  // at least one component below extdata (c.size() >= 6)
  }
  if (c.size() >= 9 && c[4] == QLatin1String("title") && isHex(c[5], 8) && isHex(c[6], 8) && c[7] == QLatin1String("data")) {
    return 8;
  }
  return -1;
}

struct Entry {
  QByteArray rel;  // UTF-8, '/'
  QString abs;
  qint64 size = 0;
  qint64 mtimeMs = 0;
};

QFileInfoList subDirs(const QString& dir) {
  return QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks, QDir::Name);
}

void walk(const QString& absDir, const QString& relDir, int depth, std::vector<Entry>* out) {
  if (depth > kMaxDepth) {
    return;
  }
  const QFileInfoList list = QDir(absDir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks, QDir::Name);
  for (const QFileInfo& fi : list) {
    const QString rel = relDir + QLatin1Char('/') + fi.fileName();
    if (fi.isDir()) {
      walk(fi.absoluteFilePath(), rel, depth + 1, out);
    } else if (fi.isFile() && !fi.isSymLink() && managedAnchor(rel) > 0) {
      out->push_back({rel.toUtf8(), fi.absoluteFilePath(), fi.size(), fi.lastModified().toMSecsSinceEpoch()});
    }
  }
}

// All managed regular files below root, sorted by relative path (byte order).
std::vector<Entry> listManaged(const QString& root) {
  std::vector<Entry> out;
  const QString base = QStringLiteral("sdmc/Nintendo 3DS");
  const QString baseAbs = QDir(root).filePath(base);
  for (const QFileInfo& id0 : subDirs(baseAbs)) {
    if (!isHex(id0.fileName(), 32)) {
      continue;
    }
    for (const QFileInfo& id1 : subDirs(id0.absoluteFilePath())) {
      if (!isHex(id1.fileName(), 32)) {
        continue;
      }
      const QString relId = base + QLatin1Char('/') + id0.fileName() + QLatin1Char('/') + id1.fileName();
      const QString extdata = QDir(id1.absoluteFilePath()).filePath(QStringLiteral("extdata"));
      if (QFileInfo(extdata).isDir() && !QFileInfo(extdata).isSymLink()) {
        walk(extdata, relId + QStringLiteral("/extdata"), 0, &out);
      }
      const QString titles = QDir(id1.absoluteFilePath()).filePath(QStringLiteral("title"));
      if (!QFileInfo(titles).isDir() || QFileInfo(titles).isSymLink()) {
        continue;
      }
      for (const QFileInfo& hi : subDirs(titles)) {
        if (!isHex(hi.fileName(), 8)) {
          continue;
        }
        for (const QFileInfo& lo : subDirs(hi.absoluteFilePath())) {
          if (!isHex(lo.fileName(), 8)) {
            continue;
          }
          const QString data = QDir(lo.absoluteFilePath()).filePath(QStringLiteral("data"));
          if (QFileInfo(data).isDir() && !QFileInfo(data).isSymLink()) {
            walk(data, relId + QStringLiteral("/title/") + hi.fileName() + QLatin1Char('/') + lo.fileName() + QStringLiteral("/data"), 0, &out);
          }
        }
      }
    }
  }
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.rel < b.rel; });
  return out;
}

void setError(QString* error, const QString& msg) {
  if (error != nullptr) {
    *error = msg;
  }
}

void putOctal(char* field, int width, quint64 value) {  // width-1 digits + NUL
  const QByteArray s = QByteArray::number(value, 8).rightJustified(width - 1, '0');
  std::memcpy(field, s.constData(), static_cast<size_t>(width - 1));
  field[width - 1] = '\0';
}

bool splitName(const QByteArray& path, QByteArray* name, QByteArray* prefix) {
  if (path.size() <= 100) {
    *name = path;
    prefix->clear();
    return true;
  }
  for (qsizetype i = 0; i < path.size(); ++i) {
    if (path.at(i) != '/') {
      continue;
    }
    if (i <= 155 && path.size() - i - 1 <= 100 && i > 0 && i < path.size() - 1) {
      *prefix = path.left(i);
      *name = path.mid(i + 1);
      return true;
    }
  }
  return false;
}

QByteArray header(const QByteArray& path, qint64 size, bool* ok) {
  QByteArray name, prefix;
  *ok = splitName(path, &name, &prefix);
  QByteArray h(kBlock, '\0');
  if (!*ok) {
    return h;
  }
  char* p = h.data();
  std::memcpy(p, name.constData(), static_cast<size_t>(name.size()));
  putOctal(p + 100, 8, 0644);
  putOctal(p + 108, 8, 0);
  putOctal(p + 116, 8, 0);
  putOctal(p + 124, 12, static_cast<quint64>(size));
  putOctal(p + 136, 12, 0);
  std::memset(p + 148, ' ', 8);
  p[156] = '0';
  std::memcpy(p + 257, "ustar\0" "00", 8);
  putOctal(p + 329, 8, 0);
  putOctal(p + 337, 8, 0);
  std::memcpy(p + 345, prefix.constData(), static_cast<size_t>(prefix.size()));
  quint32 sum = 0;
  for (int i = 0; i < kBlock; ++i) {
    sum += static_cast<unsigned char>(p[i]);
  }
  const QByteArray c = QByteArray::number(sum, 8).rightJustified(6, '0');
  std::memcpy(p + 148, c.constData(), 6);
  p[154] = '\0';
  p[155] = ' ';
  return h;
}

bool allZero(const char* p, qsizetype n) {
  for (qsizetype i = 0; i < n; ++i) {
    if (p[i] != 0) {
      return false;
    }
  }
  return true;
}

// Octal field: optional leading spaces/NULs, digits, then spaces/NULs. nullopt for anything else (incl. base-256).
std::optional<quint64> parseOctal(const char* f, int width) {
  int i = 0;
  while (i < width && (f[i] == ' ' || f[i] == '\0')) {
    ++i;
  }
  quint64 v = 0;
  int digits = 0;
  while (i < width && f[i] >= '0' && f[i] <= '7') {
    if (++digits > 21) {
      return std::nullopt;
    }
    v = (v << 3) | static_cast<quint64>(f[i] - '0');
    ++i;
  }
  while (i < width && (f[i] == ' ' || f[i] == '\0')) {
    ++i;
  }
  if (i != width || digits == 0) {
    return std::nullopt;
  }
  return v;
}

QByteArray cField(const char* f, int width) {
  const void* z = std::memchr(f, 0, static_cast<size_t>(width));
  return QByteArray(f, z != nullptr ? static_cast<qsizetype>(static_cast<const char*>(z) - f) : width);
}

struct Parsed {
  QString rel;
  qsizetype offset = 0;
  qsizetype size = 0;
};

}  // namespace

bool isManagedPath(const QString& relPath) { return managedAnchor(relPath) > 0; }

std::optional<QByteArray> pack(const QString& root, QString* error) {
  const std::vector<Entry> files = listManaged(root);
  if (files.empty()) {
    return QByteArray();
  }
  if (files.size() > static_cast<size_t>(kMaxTreeFiles)) {
    setError(error, QStringLiteral("the save tree has more than %1 files").arg(kMaxTreeFiles));
    return std::nullopt;
  }
  QByteArray out;
  qint64 total = 0;
  for (const Entry& e : files) {
    total += e.size;
    if (total > kMaxTreeBytes) {
      setError(error, QStringLiteral("the save tree is larger than %1 MiB").arg(kMaxTreeBytes / (1024 * 1024)));
      return std::nullopt;
    }
    QFile f(e.abs);
    if (!f.open(QIODevice::ReadOnly)) {
      setError(error, QStringLiteral("cannot read %1: %2").arg(QString::fromUtf8(e.rel), f.errorString()));
      return std::nullopt;
    }
    const QByteArray data = f.readAll();
    if (f.error() != QFileDevice::NoError || data.size() > kMaxTreeBytes) {
      setError(error, QStringLiteral("cannot read %1").arg(QString::fromUtf8(e.rel)));
      return std::nullopt;
    }
    bool ok = false;
    out += header(e.rel, data.size(), &ok);
    if (!ok) {
      setError(error, QStringLiteral("path too long for the archive: %1").arg(QString::fromUtf8(e.rel)));
      return std::nullopt;
    }
    out += data;
    const int pad = static_cast<int>((kBlock - data.size() % kBlock) % kBlock);
    out += QByteArray(pad, '\0');
    if (out.size() > kMaxTreeBytes + static_cast<qint64>(kMaxTreeFiles + 2) * kBlock) {
      setError(error, QStringLiteral("the save tree is too large"));
      return std::nullopt;
    }
  }
  out += QByteArray(2 * kBlock, '\0');
  return out;
}

bool unpack(const QByteArray& tar, const QString& root, QString* error) {
  const auto fail = [&](const QString& msg) {
    setError(error, msg);
    return false;
  };
  if (root.isEmpty()) {
    return fail(QStringLiteral("no target directory"));
  }
  if (tar.size() % kBlock != 0) {
    return fail(QStringLiteral("not a tar archive (size is no multiple of 512)"));
  }
  // ---- 1. parse and validate everything
  std::vector<Parsed> files;
  QSet<QString> seen;
  qint64 total = 0;
  qsizetype pos = 0;
  bool ended = false;
  while (pos < tar.size()) {
    const char* h = tar.constData() + pos;
    if (allZero(h, kBlock)) {
      ended = true;
      break;
    }
    if (pos + kBlock > tar.size()) {
      return fail(QStringLiteral("truncated archive"));
    }
    // checksum
    const auto stored = parseOctal(h + 148, 8);
    if (!stored) {
      return fail(QStringLiteral("bad header checksum"));
    }
    quint64 sum = 0;
    for (int i = 0; i < kBlock; ++i) {
      sum += (i >= 148 && i < 156) ? 32u : static_cast<unsigned char>(h[i]);
    }
    if (sum != *stored) {
      return fail(QStringLiteral("bad header checksum"));
    }
    const bool posix = std::memcmp(h + 257, "ustar\0" "00", 8) == 0;
    const bool gnu = std::memcmp(h + 257, "ustar  \0", 8) == 0;
    if (!posix && !gnu) {
      return fail(QStringLiteral("not a ustar archive"));
    }
    const char type = h[156];
    const auto size = parseOctal(h + 124, 12);
    if (!size) {
      return fail(QStringLiteral("bad size field"));
    }
    const bool regular = type == '0' || type == '\0';
    if (type == '5') {
      if (*size != 0) {
        return fail(QStringLiteral("directory entry with data"));
      }
      pos += kBlock;
      continue;
    }
    if (!regular) {
      return fail(QStringLiteral("unsupported entry type '%1'").arg(type >= 32 && type < 127 ? QString(QLatin1Char(type)) : QStringLiteral("?")));
    }
    if (*size > static_cast<quint64>(kMaxTreeBytes)) {
      return fail(QStringLiteral("the archive is larger than %1 MiB").arg(kMaxTreeBytes / (1024 * 1024)));
    }
    total += static_cast<qint64>(*size);
    if (total > kMaxTreeBytes) {
      return fail(QStringLiteral("the archive is larger than %1 MiB").arg(kMaxTreeBytes / (1024 * 1024)));
    }
    if (static_cast<int>(files.size()) >= kMaxTreeFiles) {
      return fail(QStringLiteral("the archive has more than %1 files").arg(kMaxTreeFiles));
    }
    QByteArray full = cField(h, 100);
    if (posix) {
      const QByteArray prefix = cField(h + 345, 155);
      if (!prefix.isEmpty()) {
        full = prefix + '/' + full;
      }
    }
    QString rel = QString::fromUtf8(full);
    if (rel.startsWith(QLatin1String("./"))) {
      rel.remove(0, 2);
    }
    if (!isManagedPath(rel)) {
      return fail(QStringLiteral("path not allowed in a save archive: %1").arg(rel.left(120)));
    }
    if (seen.contains(rel)) {
      return fail(QStringLiteral("duplicate path in the archive: %1").arg(rel.left(120)));
    }
    seen.insert(rel);
    const qsizetype dataLen = static_cast<qsizetype>(*size);
    const qsizetype padded = (dataLen + kBlock - 1) / kBlock * kBlock;
    if (pos + kBlock + padded > tar.size()) {
      return fail(QStringLiteral("truncated archive"));
    }
    files.push_back({rel, pos + kBlock, dataLen});
    pos += kBlock + padded;
  }
  if (ended) {
    if (!allZero(tar.constData() + pos, tar.size() - pos)) {
      return fail(QStringLiteral("unexpected data after the end of the archive"));
    }
  }
  if (files.empty()) {
    return fail(QStringLiteral("the archive contains no save files"));
  }
  // a file must not also be a directory of another file
  for (const Parsed& f : files) {
    for (qsizetype i = f.rel.indexOf(QLatin1Char('/')); i >= 0; i = f.rel.indexOf(QLatin1Char('/'), i + 1)) {
      if (seen.contains(f.rel.left(i))) {
        return fail(QStringLiteral("a path is both a file and a directory: %1").arg(f.rel.left(i).left(120)));
      }
    }
  }
  // targets must be creatable: no symlink or foreign file in the way, no directory in place of a file
  const QDir rootDir(root);
  for (const Parsed& f : files) {
    const QStringList c = f.rel.split(QLatin1Char('/'));
    QString cur = root;
    for (int i = 0; i < c.size(); ++i) {
      cur = QDir(cur).filePath(c[i]);
      const QFileInfo fi(cur);
      if (fi.isSymLink()) {
        return fail(QStringLiteral("a symbolic link is in the way: %1").arg(f.rel.left(120)));
      }
      if (fi.exists() && (i < c.size() - 1) != fi.isDir()) {
        return fail(QStringLiteral("a file and a directory collide: %1").arg(f.rel.left(120)));
      }
    }
  }
  // ---- 2. apply
  for (const Entry& e : listManaged(root)) {
    const QString rel = QString::fromUtf8(e.rel);
    if (seen.contains(rel)) {
      continue;
    }
    if (!QFile::remove(e.abs)) {
      return fail(QStringLiteral("cannot remove %1").arg(rel.left(120)));
    }
    const int anchor = managedAnchor(rel);
    QStringList c = rel.split(QLatin1Char('/'));
    c.removeLast();
    while (c.size() > anchor) {  // empty directories below data / extdata
      if (!QDir(root).rmdir(c.join(QLatin1Char('/')))) {
        break;
      }
      c.removeLast();
    }
  }
  for (const Parsed& f : files) {
    const QString path = rootDir.filePath(f.rel);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!SaveStore::atomicWrite(path, QByteArray::fromRawData(tar.constData() + f.offset, f.size))) {
      return fail(QStringLiteral("cannot write %1").arg(f.rel.left(120)));
    }
  }
  return true;
}

QString fingerprint(const QString& root) {
  const std::vector<Entry> files = listManaged(root);
  if (files.empty()) {
    return {};
  }
  QCryptographicHash h(QCryptographicHash::Sha256);
  for (const Entry& e : files) {
    h.addData(e.rel);
    h.addData(QByteArray::number(e.size) + ':' + QByteArray::number(e.mtimeMs) + '\n');
  }
  return QString::fromLatin1(h.result().toHex());
}

}  // namespace framebeam::sdtree
