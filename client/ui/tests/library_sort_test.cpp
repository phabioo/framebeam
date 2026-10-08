// Library sort, "Ready first" grouping, counts and the empty-result data of LibraryModel (3c-2) without QML.
#include <QtTest>

#include "librarymodel.h"

using namespace framebeam;
using namespace framebeam::ui;

namespace {

const QString kShaBase(63, QLatin1Char('0'));

GameEntry game(const QString& id, const QString& title, qint64 size, const QString& system, const QString& addedAt) {
  GameEntry g;
  g.id = id;
  g.title = title;
  g.system = system;
  g.romSha256 = kShaBase + id.right(1);  // ids end in a hex digit
  g.romSize = size;
  g.romFilename = id + QStringLiteral(".nds");
  g.addedAt = addedAt;
  return g;
}

RomStatus st(RomState s) {
  RomStatus r;
  r.state = s;
  return r;
}

QStringList ids(const QAbstractItemModel& m) {
  QStringList out;
  for (int i = 0; i < m.rowCount(); ++i) {
    out << m.data(m.index(i, 0), LibraryModel::GameIdRole).toString();
  }
  return out;
}

// Fixture: 6 games. a1 Zelda-like (largest, oldest), ...; statuses make a1, a3, a5 ready.
//   id  title          size  system  added       state
//   a1  Zephyr Run     900   nds     2026-01-05  ready
//   a2  alpha Garden   100   gba     2026-03-01  download needed
//   a3  Moon Courier   500   nds     2026-02-01  ready
//   a4  Beta Blocks    300   nds     (invalid)   hash mismatch  -> attention
//   a5  Orbit 10       700   gba     2026-04-01  ready
//   a6  Orbit 2        200   nds     2026-01-20  download needed
void fill(LibraryModel& m) {
  const QList<GameEntry> games{game(QStringLiteral("a1"), QStringLiteral("Zephyr Run"), 900, QStringLiteral("nds"), QStringLiteral("2026-01-05T10:00:00Z")),
                               game(QStringLiteral("a2"), QStringLiteral("alpha Garden"), 100, QStringLiteral("gba"), QStringLiteral("2026-03-01T10:00:00Z")),
                               game(QStringLiteral("a3"), QStringLiteral("Moon Courier"), 500, QStringLiteral("nds"), QStringLiteral("2026-02-01T10:00:00Z")),
                               game(QStringLiteral("a4"), QStringLiteral("Beta Blocks"), 300, QStringLiteral("nds"), QString()),
                               game(QStringLiteral("a5"), QStringLiteral("Orbit 10"), 700, QStringLiteral("gba"), QStringLiteral("2026-04-01T10:00:00Z")),
                               game(QStringLiteral("a6"), QStringLiteral("Orbit 2"), 200, QStringLiteral("nds"), QStringLiteral("2026-01-20T10:00:00Z"))};
  m.setGames(games, [](const GameEntry& g) {
    if (g.id == QLatin1String("a1") || g.id == QLatin1String("a3") || g.id == QLatin1String("a5")) return st(RomState::Ready);
    if (g.id == QLatin1String("a4")) return st(RomState::HashMismatch);
    return st(RomState::DownloadNeeded);
  });
}

}  // namespace

class LibrarySortTest : public QObject {
  Q_OBJECT
 private slots:
  void defaultIsNameAscending() {
    LibraryModel m;
    fill(m);
    QCOMPARE(m.sortKey(), QStringLiteral("name_asc"));
    // Case-insensitive, numeric-aware: "Orbit 2" before "Orbit 10"
    QCOMPARE(ids(m), (QStringList{"a2", "a4", "a3", "a6", "a5", "a1"}));
  }

  void everySortKeyBothDirections() {
    LibraryModel m;
    fill(m);
    QSignalSpy sorted(&m, &LibraryModel::sortChanged);
    const auto check = [&](const char* key, const QStringList& expected) {
      m.setSortKey(QString::fromLatin1(key));
      QCOMPARE(m.sortKey(), QString::fromLatin1(key));
      QVERIFY2(ids(m) == expected, qPrintable(QStringLiteral("%1: got %2").arg(QString::fromLatin1(key), ids(m).join(QLatin1Char(',')))));
    };
    check("name_desc", {"a1", "a5", "a6", "a3", "a4", "a2"});
    check("name_asc", {"a2", "a4", "a3", "a6", "a5", "a1"});
    // Date added; a4 has no valid date (0) and is the oldest
    check("added_desc", {"a5", "a2", "a3", "a6", "a1", "a4"});
    check("added_asc", {"a4", "a1", "a6", "a3", "a2", "a5"});
    // Size, both directions (decision ab)
    check("size_desc", {"a1", "a5", "a3", "a4", "a6", "a2"});
    check("size_asc", {"a2", "a6", "a4", "a3", "a5", "a1"});
    // System, then name: gba (alpha Garden, Orbit 10), nds (Beta, Moon, Orbit 2, Zephyr)
    check("system", {"a2", "a5", "a4", "a3", "a6", "a1"});
    QVERIFY(sorted.count() >= 7);
    m.setSortKey(QStringLiteral("bogus"));  // unknown keys are ignored
    QCOMPARE(m.sortKey(), QStringLiteral("system"));
  }

  void lastPlayedNeverPlayedComesLast() {
    LibraryModel m;
    fill(m);
    m.setSortKey(QStringLiteral("played"));
    // Nothing played yet: name order
    QCOMPARE(ids(m), (QStringList{"a2", "a4", "a3", "a6", "a5", "a1"}));
    m.setLastPlayed({{QStringLiteral("a6"), 3000}, {QStringLiteral("a1"), 9000}});
    QCOMPARE(ids(m), (QStringList{"a1", "a6", "a2", "a4", "a3", "a5"}));
    m.noteLastPlayed(QStringLiteral("a5"), 12000);  // a game starts: it jumps to the front
    QCOMPARE(ids(m), (QStringList{"a5", "a1", "a6", "a2", "a4", "a3"}));
    // Other sorts ignore it
    m.setSortKey(QStringLiteral("name_asc"));
    QCOMPARE(ids(m).first(), QStringLiteral("a2"));
  }

  void addedTextRole() {
    LibraryModel m;
    fill(m);
    const int row = m.rowOfGame(QStringLiteral("a2"));
    const QString text = m.data(m.index(row), LibraryModel::AddedTextRole).toString();
    QVERIFY2(QRegularExpression(QStringLiteral("^\\d\\d\\.\\d\\d\\.2026$")).match(text).hasMatch(), qPrintable(text));
    QVERIFY(m.data(m.index(m.rowOfGame(QStringLiteral("a4"))), LibraryModel::AddedTextRole).toString().isEmpty());
  }

  void readyFirstGroupsAndKeepsTheSortInsideGroups() {
    LibraryModel m;
    fill(m);
    QVERIFY(!m.readyFirst());
    QVERIFY(!m.grouped());
    m.setReadyFirst(true);
    // Ready: a3 Moon, a5 Orbit 10, a1 Zephyr (name order); not ready: a2, a4, a6 (name order)
    QCOMPARE(ids(m), (QStringList{"a3", "a5", "a1", "a2", "a4", "a6"}));
    QVERIFY(m.grouped());
    QCOMPARE(m.readyGroupCount(), 3);
    QCOMPARE(m.notReadyGroupCount(), 3);
    // Same sort inside both groups
    m.setSortKey(QStringLiteral("size_desc"));
    QCOMPARE(ids(m), (QStringList{"a1", "a5", "a3", "a4", "a6", "a2"}));
    m.setSortKey(QStringLiteral("name_desc"));
    QCOMPARE(ids(m), (QStringList{"a1", "a5", "a3", "a6", "a4", "a2"}));
    // Group roles
    QVERIFY(m.data(m.index(0), LibraryModel::ReadyGroupRole).toBool());
    QVERIFY(!m.data(m.index(3), LibraryModel::ReadyGroupRole).toBool());
    m.setReadyFirst(false);
    QVERIFY(!m.grouped());
    QCOMPARE(ids(m), (QStringList{"a1", "a5", "a6", "a3", "a4", "a2"}));
  }

  void readyFirstWithFilterAndSearch() {
    LibraryModel m;
    fill(m);
    m.setReadyFirst(true);
    // Search "i": Moon Courier (a3, ready), Orbit 10 (a5, ready), Orbit 2 (a6, not ready)
    m.setFilterText(QStringLiteral("i"));
    QCOMPARE(ids(m), (QStringList{"a3", "a5", "a6"}));
    QVERIFY(m.grouped());
    QCOMPARE(m.readyGroupCount(), 2);
    QCOMPARE(m.notReadyGroupCount(), 1);
    // Chip "Ready": every result is ready, grouping is not applicable and no divider is shown
    m.setFilter(QStringLiteral("ready"));
    QVERIFY(!m.readyFirstApplicable());
    QVERIFY(!m.grouped());
    QCOMPARE(ids(m), (QStringList{"a3", "a5"}));
    // Chip "Not downloaded": only one group, no divider although Ready first is on
    m.setFilter(QStringLiteral("download"));
    QVERIFY(m.readyFirstApplicable());
    QVERIFY(!m.grouped());
    QCOMPARE(ids(m), (QStringList{"a6"}));
    // Chip "All" again with the search cleared
    m.setFilter(QStringLiteral("all"));
    m.setFilterText(QString());
    QVERIFY(m.grouped());
    QCOMPARE(ids(m).size(), 6);
  }

  void syncPendingIsNotReady() {
    LibraryModel m;
    fill(m);
    m.setReadyFirst(true);
    QVERIFY(m.isReady(QStringLiteral("a3")));
    m.setSyncKind(QStringLiteral("a3"), QStringLiteral("pending"));  // decision aa
    QVERIFY(!m.isReady(QStringLiteral("a3")));
    QCOMPARE(ids(m), (QStringList{"a5", "a1", "a2", "a4", "a3", "a6"}));
    QCOMPARE(m.notReadyGroupCount(), 4);
    m.setSyncKind(QStringLiteral("a3"), QStringLiteral("synced"));
    QVERIFY(m.isReady(QStringLiteral("a3")));
    QCOMPARE(m.readyGroupCount(), 3);
    // A status change moves a game between the groups too
    m.setStatus(m.game(QStringLiteral("a2"))->romSha256, st(RomState::Ready));
    QCOMPARE(ids(m), (QStringList{"a2", "a3", "a5", "a1", "a4", "a6"}));
  }

  void groupProxySplitsTheModel() {
    LibraryModel m;
    fill(m);
    m.setReadyFirst(true);
    LibraryGroupModel ready, rest, all;
    ready.setLibrary(&m);
    rest.setLibrary(&m);
    all.setLibrary(&m);
    ready.setGroup(0);
    rest.setGroup(1);
    all.setGroup(-1);
    QCOMPARE(ids(ready), (QStringList{"a3", "a5", "a1"}));
    QCOMPARE(ids(rest), (QStringList{"a2", "a4", "a6"}));
    QCOMPARE(ids(all).size(), 6);
    m.setSortKey(QStringLiteral("size_asc"));
    QCOMPARE(ids(ready), (QStringList{"a3", "a5", "a1"}));
    QCOMPARE(ids(rest), (QStringList{"a2", "a6", "a4"}));
    m.setStatus(m.game(QStringLiteral("a6"))->romSha256, st(RomState::Ready));
    QCOMPARE(ids(ready), (QStringList{"a6", "a3", "a5", "a1"}));
    QCOMPARE(ids(rest), (QStringList{"a2", "a4"}));
  }

  void countsAndToolbarTextFollowFilterAndSearch() {
    LibraryModel m;
    fill(m);
    m.setSystemLabel(QStringLiteral("Nintendo DS"));
    QCOMPARE(m.countText(), QStringLiteral("6 games · Nintendo DS"));
    m.setFilter(QStringLiteral("ready"));
    QCOMPARE(m.countText(), QStringLiteral("3 of 6 games"));  // decision ac
    m.setFilterText(QStringLiteral("zzz"));
    QCOMPARE(m.countText(), QStringLiteral("0 of 3 ready games"));
    // Chips recount to the search result
    QCOMPARE(m.counts().value(QStringLiteral("all")).toInt(), 0);
    m.setFilterText(QStringLiteral("beta"));
    QCOMPARE(m.counts().value(QStringLiteral("all")).toInt(), 1);
    QCOMPARE(m.counts().value(QStringLiteral("ready")).toInt(), 0);
    QCOMPARE(m.counts().value(QStringLiteral("attention")).toInt(), 1);
    QCOMPARE(m.countText(), QStringLiteral("0 of 3 ready games"));
    // The only match is outside the filter
    const QVariantMap out = m.outsideFilter();
    QCOMPARE(out.value(QStringLiteral("count")).toInt(), 1);
    QCOMPARE(out.value(QStringLiteral("title")).toString(), QStringLiteral("Beta Blocks"));
    QCOMPARE(out.value(QStringLiteral("reason")).toString(), QStringLiteral("needs attention · hash mismatch"));
    m.setFilter(QStringLiteral("all"));
    QVERIFY(m.outsideFilter().isEmpty());
    QCOMPARE(m.countText(), QStringLiteral("1 of 6 games"));
    m.setFilterText(QString());
    QCOMPARE(m.countText(), QStringLiteral("6 games · Nintendo DS"));
    m.setSystemLabel(QString());
    QCOMPARE(m.countText(), QStringLiteral("6 games"));
  }
};

QTEST_GUILESS_MAIN(LibrarySortTest)
#include "library_sort_test.moc"
