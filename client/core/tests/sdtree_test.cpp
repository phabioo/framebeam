// 3DS SD tree <-> deterministic ustar archive (dummy bytes only).
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "sdtree.h"

using namespace uzume;

namespace {
const QString kId0 = QStringLiteral("0123456789ABCDEF0123456789ABCDEF");
const QString kId1 = QStringLiteral("FEDCBA9876543210FEDCBA9876543210");
const QString kBase = QStringLiteral("sdmc/Nintendo 3DS/") + kId0 + QLatin1Char('/') + kId1;
const QString kMain = kBase + QStringLiteral("/title/00040000/00123400/data/00000001/main");
const QString kMeta = kBase + QStringLiteral("/title/00040000/00123400/data/00000001/.metadata");
const QString kExt = kBase + QStringLiteral("/extdata/00000000/0000abcd/user/slot1.bin");

void put(const QString& root, const QString& rel, const QByteArray& data) {
  const QString p = QDir(root).filePath(rel);
  QDir().mkpath(QFileInfo(p).absolutePath());
  QFile f(p);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(data);
}
QByteArray get(const QString& root, const QString& rel) {
  QFile f(QDir(root).filePath(rel));
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
bool has(const QString& root, const QString& rel) { return QFileInfo::exists(QDir(root).filePath(rel)); }

// Independent tar writer for crafting (bad) archives.
QByteArray block(const QByteArray& name, qint64 size, char type, const QByteArray& magic = QByteArray("ustar\0" "00", 8), const QByteArray& prefix = QByteArray(),
                 bool badSum = false) {
  QByteArray h(512, '\0');
  h.replace(0, name.size(), name);
  h.replace(100, 8, QByteArray("0000644\0", 8));
  h.replace(124, 12, QByteArray::number(size, 8).rightJustified(11, '0') + '\0');
  h.replace(148, 8, QByteArray(8, ' '));
  h[156] = type;
  h.replace(257, 8, magic);
  h.replace(345, prefix.size(), prefix);
  quint32 sum = 0;
  for (char c : h) sum += static_cast<unsigned char>(c);
  h.replace(148, 8, QByteArray::number(sum + (badSum ? 1 : 0), 8).rightJustified(6, '0') + QByteArray("\0 ", 2));
  return h;
}
QByteArray entry(const QByteArray& path, const QByteArray& data, char type = '0', const QByteArray& magic = QByteArray("ustar\0" "00", 8)) {
  QByteArray name = path, prefix;
  for (qsizetype i = 1; path.size() > 100 && i < path.size(); ++i) {  // split a long path like a real tar does
    if (path.at(i) == '/' && path.size() - i - 1 <= 100) {
      prefix = path.left(i);
      name = path.mid(i + 1);
      break;
    }
  }
  QByteArray e = block(name, data.size(), type, magic, prefix) + data;
  e += QByteArray((512 - data.size() % 512) % 512, '\0');
  return e;
}
const QByteArray kEnd(1024, '\0');
}  // namespace

class SdTreeTest : public QObject {
  Q_OBJECT
  QString fill(QTemporaryDir& d) {
    put(d.path(), kMain, "main-save");
    put(d.path(), kMeta, "meta");
    put(d.path(), kExt, "ext");
    return d.path();
  }
  void expectRejected(const QByteArray& tar, const QString& needle = QString()) {
    QTemporaryDir d;
    fill(d);
    const QString before = sdtree::fingerprint(d.path());
    put(d.path(), QStringLiteral("nand/data/keep"), "n");
    QString err;
    QVERIFY(!sdtree::unpack(tar, d.path(), &err));
    QVERIFY(!err.isEmpty());
    if (!needle.isEmpty()) QVERIFY2(err.contains(needle), qPrintable(err));
    QCOMPARE(sdtree::fingerprint(d.path()), before);  // untouched
    QCOMPARE(get(d.path(), kMain), QByteArray("main-save"));
    QVERIFY(has(d.path(), QStringLiteral("nand/data/keep")));
  }
 private slots:
  void noManagedFilesPackToNothing() {
    QTemporaryDir d;
    put(d.path(), QStringLiteral("nand/x"), "n");
    QString err;
    const auto t = sdtree::pack(d.path(), &err);
    QVERIFY(t.has_value());
    QVERIFY(t->isEmpty());
    QVERIFY(sdtree::fingerprint(d.path()).isEmpty());
  }
  void packIsDeterministicAndIgnoresMtime() {
    QTemporaryDir a, b;
    fill(a);
    fill(b);
    QFile f(QDir(b.path()).filePath(kMain));
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(QDateTime::currentDateTimeUtc().addDays(-400), QFileDevice::FileModificationTime));
    f.close();
    const auto ta = sdtree::pack(a.path());
    const auto tb = sdtree::pack(b.path());
    QVERIFY(ta && tb && !ta->isEmpty());
    QCOMPARE(*ta, *tb);
    QCOMPARE(*sdtree::pack(a.path()), *ta);
    QCOMPARE(ta->size() % 512, 0);
    QCOMPARE(ta->mid(257, 8), QByteArray("ustar\0" "00", 8));
    QVERIFY(ta->endsWith(kEnd));
    QVERIFY(sdtree::fingerprint(a.path()) != sdtree::fingerprint(b.path()));  // mtime is part of the fingerprint
  }
  void packSelectsManagedFilesOnly() {
    QTemporaryDir d;
    fill(d);
    put(d.path(), QStringLiteral("nand/data/x"), "nand");
    put(d.path(), QStringLiteral("sysdata/x"), "sys");
    put(d.path(), QStringLiteral("shaders/x"), "sh");
    put(d.path(), QStringLiteral("cheats/x"), "ch");
    put(d.path(), kBase + QStringLiteral("/title/00040000/00123400/content/00000000.app"), "content");
    put(d.path(), QStringLiteral("sdmc/Nintendo 3DS/") + kId0 + QStringLiteral("/other.bin"), "x");
    put(d.path(), QStringLiteral("sdmc/Nintendo 3DS/zz/") + kId1 + QStringLiteral("/extdata/a"), "x");
    const QByteArray t = *sdtree::pack(d.path());
    QVERIFY(t.contains("main-save"));
    QVERIFY(t.contains("meta"));  // .metadata
    QVERIFY(t.contains(".metadata"));
    QVERIFY(t.contains("extdata/00000000/0000abcd/user/slot1.bin"));
    for (const char* s : {"nand", "sysdata", "shaders", "cheats", "content"}) QVERIFY2(!t.contains(s), s);
    QVERIFY(!t.contains("other.bin"));
    QVERIFY(!t.contains("/zz/"));
    // sorted by path: extdata < title
    QVERIFY(t.indexOf("extdata/") < t.indexOf("title/"));
  }
  void symlinksAreSkipped() {
#ifndef Q_OS_WIN
    QTemporaryDir d;
    fill(d);
    QVERIFY(QFile::link(QDir(d.path()).filePath(kMain), QDir(d.path()).filePath(kMain + QStringLiteral("2"))));
    const QByteArray t = *sdtree::pack(d.path());
    QVERIFY(!t.contains("main2"));
#endif
  }
  void roundTripAndLongPathPrefix() {
    QTemporaryDir a;
    fill(a);
    const QString deep = kBase + QStringLiteral("/extdata/00000000/0000abcd/") + QString(20, QLatin1Char('d')) + QLatin1Char('/') + QString(20, QLatin1Char('e')) + QLatin1Char('/') + QString(70, QLatin1Char('f'));
    QVERIFY(deep.toUtf8().size() > 100 && deep.toUtf8().size() < 255);
    put(a.path(), deep, "deep-data");
    put(a.path(), kExt + QStringLiteral("\xc3\xa4"), "umlaut");  // UTF-8 name
    const auto packed = sdtree::pack(a.path());
    QVERIFY(packed.has_value());
    const QByteArray t = *packed;
    QVERIFY(t.mid(345, 155).contains("extdata"));  // prefix field in use
    QTemporaryDir b;
    put(b.path(), QStringLiteral("nand/keep"), "n");
    QString err;
    QVERIFY2(sdtree::unpack(t, b.path(), &err), qPrintable(err));
    QCOMPARE(get(b.path(), deep), QByteArray("deep-data"));
    QCOMPARE(get(b.path(), kMain), QByteArray("main-save"));
    QCOMPARE(get(b.path(), kMeta), QByteArray("meta"));
    QCOMPARE(get(b.path(), kExt), QByteArray("ext"));
    QCOMPARE(get(b.path(), QStringLiteral("nand/keep")), QByteArray("n"));
    QCOMPARE(*sdtree::pack(b.path()), t);
  }
  void unpackRemovesStaleManagedFilesButNotUnmanaged() {
    QTemporaryDir a, b;
    fill(a);
    fill(b);
    put(b.path(), kBase + QStringLiteral("/title/00040000/00123400/data/00000001/old"), "stale");
    put(b.path(), kBase + QStringLiteral("/extdata/00000000/00000001/dir/stale"), "stale");
    put(b.path(), kBase + QStringLiteral("/title/00040000/00123400/content/c.app"), "content");
    put(b.path(), QStringLiteral("nand/data/keep"), "n");
    put(b.path(), kMain, "changed-locally");
    QString err;
    QVERIFY2(sdtree::unpack(*sdtree::pack(a.path()), b.path(), &err), qPrintable(err));
    QVERIFY(!has(b.path(), kBase + QStringLiteral("/title/00040000/00123400/data/00000001/old")));
    QVERIFY(!has(b.path(), kBase + QStringLiteral("/extdata/00000000/00000001/dir/stale")));
    QVERIFY(!has(b.path(), kBase + QStringLiteral("/extdata/00000000/00000001")));  // emptied directories pruned
    QVERIFY(has(b.path(), kBase + QStringLiteral("/extdata")));
    QVERIFY(has(b.path(), kBase + QStringLiteral("/title/00040000/00123400/content/c.app")));
    QVERIFY(has(b.path(), QStringLiteral("nand/data/keep")));
    QCOMPARE(get(b.path(), kMain), QByteArray("main-save"));
  }
  void unpackAcceptsDotSlashGnuMagicAndDirectories() {
    QTemporaryDir d;
    QByteArray tar = entry("./sdmc/", {}, '5') + entry(("./" + kMain).toUtf8(), {}, '5');  // dirs are ignored
    // a GNU-magic file whose name fits 100 bytes
    const QString shortExt = kBase + QStringLiteral("/extdata/s");
    const QByteArray name = "./" + shortExt.toUtf8();
    QVERIFY(name.size() <= 100);
    tar += entry(name, "g", '\0', QByteArray("ustar  \0", 8)) + kEnd;
    QString err;
    QVERIFY2(sdtree::unpack(tar, d.path(), &err), qPrintable(err));
    QCOMPARE(get(d.path(), shortExt), QByteArray("g"));
  }
  void unpackRejectsBadArchives() {
    const QByteArray ok = entry(kExt.toUtf8(), "x");
    expectRejected(entry("../evil", "x") + kEnd, "not allowed");
    expectRejected(entry((kBase + "/extdata/../../../evil").toUtf8(), "x") + kEnd, "not allowed");
    expectRejected(entry("/etc/passwd", "x") + kEnd, "not allowed");
    expectRejected(entry(("/" + kExt).toUtf8(), "x") + kEnd, "not allowed");
    expectRejected(entry("nand/data/x", "x") + kEnd, "not allowed");
    expectRejected(entry(("sdmc/Nintendo 3DS/" + kId0 + "/" + kId1 + "/title/00040000/00123400/content/x").toUtf8(), "x") + kEnd);
    expectRejected(entry((kBase + "/extdata/a:b").toUtf8(), "x") + kEnd);
    expectRejected(entry((kBase + "/extdata/NUL").toUtf8(), "x") + kEnd);
    expectRejected(entry((kBase + "/extdata/a\\b").toUtf8(), "x") + kEnd);
    for (char t : {'1', '2', '3', '4', '6', '7', 'x', 'g', 'L', 'K'}) {
      expectRejected(entry(kExt.toUtf8(), "x", t) + kEnd, "type");  // hardlink, symlink, devices, fifo, pax, GNU longname
    }
    expectRejected(ok.left(100) + ok.mid(100, 48) + QByteArray("junk") + ok.mid(152) + kEnd, "checksum");
    expectRejected(block(kExt.toUtf8(), 1, '0', QByteArray("ustar\0" "00", 8), {}, true) + QByteArray(512, 'x') + kEnd, "checksum");
    expectRejected(entry(kExt.toUtf8(), "x", '0', QByteArray(8, '\0')) + kEnd, "ustar");
    expectRejected(ok + ok + kEnd, "duplicate");
    expectRejected(ok + QByteArray("trailing") + QByteArray(512 - 8, '\0') + kEnd);
    expectRejected(ok + kEnd + entry(kMain.toUtf8(), "x"));  // data after the end
    expectRejected(ok.left(700), "multiple");
    expectRejected(ok.left(512 + 0) + QByteArray(), QString());  // header without its data
    expectRejected(kEnd, "no save files");
    expectRejected(QByteArray(), "no save files");
    expectRejected(entry((kBase + "/extdata/a").toUtf8(), "x") + entry((kBase + "/extdata/a/b").toUtf8(), "y") + kEnd, "file and a directory");
    // oversize: declared size above the limit (no data needed), and several files adding up above it
    expectRejected(block(kExt.toUtf8(), sdtree::kMaxTreeBytes + 1, '0') + kEnd, "larger");
    {
      const QByteArray half(static_cast<int>(sdtree::kMaxTreeBytes / 2 + 512), 'z');
      expectRejected(entry((kBase + "/extdata/a").toUtf8(), half) + entry((kBase + "/extdata/b").toUtf8(), half) + kEnd, "larger");
    }
    {
      QByteArray many;
      for (int i = 0; i <= sdtree::kMaxTreeFiles; ++i) many += entry((kBase + "/extdata/f" + QByteArray::number(i)).toUtf8(), {});
      expectRejected(many + kEnd, "more than");
    }
  }
  void packRejectsOversizeTree() {
    QTemporaryDir d;
    put(d.path(), kExt, QByteArray(static_cast<int>(sdtree::kMaxTreeBytes + 1), 'o'));
    QString err;
    QVERIFY(!sdtree::pack(d.path(), &err).has_value());
    QVERIFY(!err.isEmpty());
  }
  void unpackRefusesASymlinkedDirectoryInTheWay() {
#ifndef Q_OS_WIN
    QTemporaryDir d, outside;
    QTemporaryDir src;
    fill(src);
    QDir().mkpath(QDir(d.path()).filePath(QStringLiteral("sdmc")));
    QVERIFY(QFile::link(outside.path(), QDir(d.path()).filePath(QStringLiteral("sdmc/Nintendo 3DS"))));
    QString err;
    QVERIFY(!sdtree::unpack(*sdtree::pack(src.path()), d.path(), &err));
    QVERIFY(QDir(outside.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
#endif
  }
  void managedPathRules() {
    QVERIFY(sdtree::isManagedPath(kMain));
    QVERIFY(sdtree::isManagedPath(kExt));
    QVERIFY(!sdtree::isManagedPath(kBase + QStringLiteral("/extdata")));
    QVERIFY(!sdtree::isManagedPath(kBase + QStringLiteral("/title/00040000/00123400/data")));
    QVERIFY(!sdtree::isManagedPath(kBase + QStringLiteral("/title/0004/00123400/data/x")));
    QVERIFY(!sdtree::isManagedPath(kBase + QStringLiteral("/extdata//x")));
    QVERIFY(!sdtree::isManagedPath(kBase + QStringLiteral("/extdata/./x")));
    QVERIFY(!sdtree::isManagedPath(QStringLiteral("sdmc/Nintendo 3DS/abc/def/extdata/x")));
  }
  void fingerprintTracksChanges() {
    QTemporaryDir d;
    fill(d);
    const QString f1 = sdtree::fingerprint(d.path());
    QCOMPARE(sdtree::fingerprint(d.path()), f1);
    put(d.path(), QStringLiteral("nand/x"), "unmanaged");
    QCOMPARE(sdtree::fingerprint(d.path()), f1);
    put(d.path(), kMain, "main-save-longer");
    QVERIFY(sdtree::fingerprint(d.path()) != f1);
  }
};

QTEST_GUILESS_MAIN(SdTreeTest)
#include "sdtree_test.moc"
