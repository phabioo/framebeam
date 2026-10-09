// Save sync in the UI with the real melonDS DS core and the homebrew test ROM (NEEDS_CORE: 77 without a core):
// conflict dialog (3d) with keyboard operation, decide later, Use Hub version, library state badge.
#include <QFile>
#include <QSignalSpy>
#include <QtTest>

#include "fakehub.h"
#include "savestore.h"
#include "testsupport.h"

using namespace framebeam;
using namespace framebeam::ui;
using uitest::Harness;

namespace {
QByteArray readFile(const QString& p) {
  QFile f(p);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
void writeFile(const QString& p, const QByteArray& d) {
  QDir().mkpath(QFileInfo(p).absolutePath());
  QFile f(p);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(d);
}
}  // namespace

class SaveUiTest : public QObject {
  Q_OBJECT
 private slots:
  void conflictDialogAndLibraryState() {
    if (qEnvironmentVariableIsEmpty("FRAMEBEAM_MELONDS_DS_CORE")) {
      QSKIP("FRAMEBEAM_MELONDS_DS_CORE not set");
    }
    uitest::installWarningCounter();
    QFile rom(QStringLiteral(FB_TEST_ROM_PATH));
    QVERIFY(rom.open(QIODevice::ReadOnly));
    const QByteArray romData = rom.readAll();
    const QString sha = uitest::sha256Hex(romData);

    FakeHub hub(QStringLiteral("a"));
    hub.hubId = QStringLiteral("hub-save");
    hub.decision = FakeHub::Decision::Approve;
    hub.games = QJsonObject{{QStringLiteral("games"),
                             QJsonArray{uitest::gameJson(QStringLiteral("t1"), QStringLiteral("Harbor Rally"), sha, romData.size(),
                                                         QStringLiteral("framebeam_test.nds"))}}};
    hub.roms.insert(sha, romData);
    hub.setHubSave(QStringLiteral("t1"), "hub-save-bytes");
    QVERIFY(hub.start());

    Harness h;
    QVERIFY(h.start(true));
    h.controller->addHub(hub.address());
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::NeedsTrustConfirmation);
    h.controller->confirmTrust();
    QTRY_COMPARE(h.controller->connection()->state(), HubConnection::State::NeedsPairing);
    h.controller->requestPairing();
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("library"), 8000);
    QTRY_COMPARE(h.controller->libraryState(), QStringLiteral("ready"));
    QVERIFY(h.controller->saveNote().isEmpty());

    // A local save from an earlier offline session (never synced)
    const QString dir = SaveStore::gameDir(*h.controller->profileStore(), QStringLiteral("hub-save"), QStringLiteral("u_test_1"), QStringLiteral("t1"));
    const QString saveFile = dir + QLatin1Char('/') + sha + QStringLiteral(".sav");
    writeFile(saveFile, "local-offline-bytes");

    // Play -> ROM download -> start sync -> conflict dialog BEFORE the core starts
    h.controller->playSelected();
    QTRY_VERIFY_WITH_TIMEOUT(h.controller->saveConflict().value(QStringLiteral("active")).toBool(), 20000);
    QCOMPARE(h.controller->screen(), QStringLiteral("library"));
    QCOMPARE(h.controller->gameSession()->state(), GameSession::Idle);
    QQuickItem* dialog = h.item("conflictDialog");
    QVERIFY(dialog != nullptr);
    QTRY_VERIFY(dialog->isVisible());
    const QVariantMap c = h.controller->saveConflict();
    QVERIFY(c.value(QStringLiteral("hub")).toMap().value(QStringLiteral("title")).toString().contains(QStringLiteral("Laptop Office")));
    QVERIFY(c.value(QStringLiteral("hub")).toMap().value(QStringLiteral("when")).toString().contains(QStringLiteral("Rev 1")));
    QVERIFY(c.value(QStringLiteral("local")).toMap().value(QStringLiteral("when")).toString().contains(QStringLiteral("sync pending")));
    uitest::saveShot(h.window, QStringLiteral("3d-save-conflict"));
    // Default action focused, keyboard operable
    QQuickItem* keep = h.item("keepBothButton");
    QVERIFY(keep != nullptr);
    QTRY_VERIFY(keep->hasActiveFocus());
    QTest::keyClick(h.window, Qt::Key_Tab);
    QVERIFY(h.item("useHubButton")->hasActiveFocus());
    QTest::keyClick(h.window, Qt::Key_Backtab);
    QVERIFY(keep->hasActiveFocus());
    QCOMPARE(readFile(saveFile), QByteArray("local-offline-bytes"));  // nothing overwritten while the dialog is open

    // Esc = decide later: the game starts with the LOCAL save, conflict stays
    QTest::keyClick(h.window, Qt::Key_Escape);
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("game"), 20000);
    QVERIFY(h.controller->saveConflict().isEmpty());
    QCOMPARE(readFile(saveFile), QByteArray("local-offline-bytes"));
    QVERIFY(!SaveStore::loadState(dir).conflictId.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(h.controller->gameSession()->frameNumber() >= 3, 10000);
    QVERIFY(h.click("endGameButton"));
    QTRY_COMPARE(h.controller->screen(), QStringLiteral("library"));
    QCOMPARE(h.controller->library()->syncKind(QStringLiteral("t1")), QStringLiteral("conflict"));
    QCOMPARE(h.controller->selectedGame().value(QStringLiteral("saveText")).toString(), QStringLiteral("Conflict"));
    QCOMPARE(hub.saves.value(QStringLiteral("t1")).revision, 1);  // Hub current unchanged

    // Next start: dialog again; "Use Hub version" replaces the local file (backup kept)
    h.controller->playSelected();
    QTRY_VERIFY_WITH_TIMEOUT(h.controller->saveConflict().value(QStringLiteral("active")).toBool(), 20000);
    QTRY_VERIFY(h.item("useHubButton")->isVisible());
    QVERIFY(h.click("useHubButton"));
    QTRY_COMPARE_WITH_TIMEOUT(h.controller->screen(), QStringLiteral("game"), 20000);
    QCOMPARE(readFile(saveFile).left(14), QByteArray("hub-save-bytes"));
    QCOMPARE(QDir(dir).entryList({QStringLiteral("*.local-*.bak")}).size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(h.controller->gameSession()->frameNumber() >= 3, 10000);
    // Pause = immediate sync check, stop = final sync
    QVERIFY(h.click("endGameButton"));
    QTRY_COMPARE(h.controller->library()->syncKind(QStringLiteral("t1")), QStringLiteral("synced"));
    qInfo().noquote() << "Files in the game save dir after play:" << QDir(dir).entryList(QDir::Files).join(QLatin1Char(' '));
    QCOMPARE(uitest::warningCount().load(), 0);
  }
};

UITEST_MAIN(SaveUiTest)
#include "save_ui_test.moc"
