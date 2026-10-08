// Tests without QML: keyboard mapping, touch conversion, scaling, resampler, LibraryModel.
#include <QSignalSpy>
#include <QtTest>
#include <cmath>

#include "audioresampler.h"
#include "emulator_backend.h"
#include "gamesession.h"
#include "inputmapping.h"
#include "librarymodel.h"
#include "system_manifest.h"

using namespace framebeam;
using namespace framebeam::ui;
using emu::JoypadButton;
using emu::buttonMask;

namespace {

emu::DisplayProfile ndsProfile() {
  emu::DisplayProfile p;
  p.layout = QStringLiteral("vertical");
  p.gap = 0;
  p.screens = {{QStringLiteral("top"), 256, 192, false}, {QStringLiteral("bottom"), 256, 192, true}};
  return p;
}

QByteArray stereo(const QList<qint16>& v) {
  return QByteArray(reinterpret_cast<const char*>(v.constData()), v.size() * 2);
}

GameEntry game(const QString& id, const QString& title, const QString& sha, qint64 size) {
  GameEntry g;
  g.id = id;
  g.title = title;
  g.system = QStringLiteral("nds");
  g.romSha256 = sha;
  g.romSize = size;
  g.romFilename = id + QStringLiteral(".nds");
  return g;
}

RomStatus st(RomState s, qint64 got = 0, qint64 total = 0) {
  RomStatus r;
  r.state = s;
  r.receivedBytes = got;
  r.totalBytes = total;
  return r;
}

}  // namespace

class InputTest : public QObject {
  Q_OBJECT
 private slots:
  // D4: F3, F5, F11 and Esc belong to the Player; even a profile that maps them never reaches the core.
  void reservedKeysAreNeverForwarded() {
    GameSession gs;
    QHash<int, quint32> map;
    for (int k : {int(Qt::Key_F3), int(Qt::Key_F5), int(Qt::Key_F11), int(Qt::Key_Escape), int(Qt::Key_Space)}) {
      map.insert(k, buttonMask(JoypadButton::A));
    }
    map.insert(Qt::Key_Z, buttonMask(JoypadButton::B));
    gs.setKeyboardMap(map);
    for (int k : {int(Qt::Key_F3), int(Qt::Key_F5), int(Qt::Key_F11), int(Qt::Key_Escape), int(Qt::Key_Space)}) {
      QVERIFY(gs.isReservedKey(k));
      QVERIFY(!gs.keyEvent(k, true));
      QVERIFY(!gs.keyEvent(k, false));
    }
    QVERIFY(!gs.isReservedKey(Qt::Key_Z));
    QVERIFY(gs.keyEvent(Qt::Key_Z, true));
    QVERIFY(gs.keyEvent(Qt::Key_Z, false));
  }

  // Configurable hotkeys: the remapped key is reserved (and not forwarded), the old default key is a normal key again.
  void remappedHotkeyIsReserved() {
    GameSession gs;
    gs.setHotkeyKeys({Qt::Key_F10, Qt::Key_F3, Qt::Key_F5});
    QHash<int, quint32> map{{Qt::Key_F10, buttonMask(JoypadButton::A)}, {Qt::Key_F11, buttonMask(JoypadButton::B)}};
    gs.setKeyboardMap(map);
    QVERIFY(gs.isReservedKey(Qt::Key_F10));
    QVERIFY(!gs.keyEvent(Qt::Key_F10, true));
    QVERIFY(!gs.isReservedKey(Qt::Key_F11));
    QVERIFY(gs.keyEvent(Qt::Key_F11, true));
    QVERIFY(gs.isReservedKey(Qt::Key_Escape));  // fixed
    gs.setHotkeyKeys({});
    QVERIFY(gs.isReservedKey(Qt::Key_Escape));
    QVERIFY(!gs.isReservedKey(Qt::Key_F3));
  }

  // Keyboard profile map: replaces the standard map; two keys on one button keep it pressed until both are up.
  void customKeyboardMap() {
    KeyboardJoypad k;
    QVERIFY(k.press(Qt::Key_X));
    QCOMPARE(k.mask(), buttonMask(JoypadButton::A));
    k.setMap({{Qt::Key_J, buttonMask(JoypadButton::A)}, {Qt::Key_K, buttonMask(JoypadButton::A)}, {Qt::Key_L, buttonMask(JoypadButton::B)}});
    QCOMPARE(k.mask(), 0u);  // X is not mapped any more (it was held: re-evaluated)
    QVERIFY(!k.press(Qt::Key_X));
    QVERIFY(k.press(Qt::Key_J));
    QVERIFY(k.press(Qt::Key_K));
    QVERIFY(k.release(Qt::Key_J));
    QCOMPARE(k.mask(), buttonMask(JoypadButton::A));
    QVERIFY(k.press(Qt::Key_L));
    QCOMPARE(k.mask(), buttonMask(JoypadButton::A) | buttonMask(JoypadButton::B));
    k.release(Qt::Key_K);
    k.release(Qt::Key_L);
    QCOMPARE(k.mask(), 0u);
    k.useStandardMap();
    QVERIFY(k.press(Qt::Key_X));
    QCOMPARE(k.mask(), buttonMask(JoypadButton::A));
  }

  void keyMapping() {
    QCOMPARE(joypadMaskForKey(Qt::Key_Up), buttonMask(JoypadButton::Up));
    QCOMPARE(joypadMaskForKey(Qt::Key_Down), buttonMask(JoypadButton::Down));
    QCOMPARE(joypadMaskForKey(Qt::Key_Left), buttonMask(JoypadButton::Left));
    QCOMPARE(joypadMaskForKey(Qt::Key_Right), buttonMask(JoypadButton::Right));
    QCOMPARE(joypadMaskForKey(Qt::Key_X), buttonMask(JoypadButton::A));
    QCOMPARE(joypadMaskForKey(Qt::Key_Z), buttonMask(JoypadButton::B));
    QCOMPARE(joypadMaskForKey(Qt::Key_S), buttonMask(JoypadButton::X));
    QCOMPARE(joypadMaskForKey(Qt::Key_A), buttonMask(JoypadButton::Y));
    QCOMPARE(joypadMaskForKey(Qt::Key_Q), buttonMask(JoypadButton::L));
    QCOMPARE(joypadMaskForKey(Qt::Key_W), buttonMask(JoypadButton::R));
    QCOMPARE(joypadMaskForKey(Qt::Key_Return), buttonMask(JoypadButton::Start));
    QCOMPARE(joypadMaskForKey(Qt::Key_Enter), buttonMask(JoypadButton::Start));
    QCOMPARE(joypadMaskForKey(Qt::Key_Backspace), buttonMask(JoypadButton::Select));
    QCOMPARE(joypadMaskForKey(Qt::Key_Escape), 0u);  // Esc = pause, no joypad button
    QCOMPARE(joypadMaskForKey(Qt::Key_F1), 0u);
  }

  void keyboardState() {
    KeyboardJoypad kb;
    QVERIFY(kb.press(Qt::Key_Up));
    QVERIFY(kb.press(Qt::Key_X));
    QCOMPARE(kb.mask(), buttonMask(JoypadButton::Up) | buttonMask(JoypadButton::A));
    QVERIFY(kb.release(Qt::Key_Up));
    QCOMPARE(kb.mask(), buttonMask(JoypadButton::A));
    QVERIFY(!kb.press(Qt::Key_F5));
    QCOMPARE(kb.mask(), buttonMask(JoypadButton::A));
    kb.clear();
    QCOMPARE(kb.mask(), 0u);
  }

  void fitFrameScaling() {
    // 256x384 fits 2x in 600x800 (not 3x: 768 > 600).
    QRectF r = fitFrame(QSizeF(256, 384), QSizeF(600, 800), true);
    QCOMPARE(r.size(), QSizeF(512, 768));
    QCOMPARE(r.x(), 44.0);
    QCOMPARE(r.y(), 16.0);
    // Without integer scaling: factor 800/384.
    r = fitFrame(QSizeF(256, 384), QSizeF(600, 800), false);
    QCOMPARE(r.height(), 800.0);
    QVERIFY(std::abs(r.width() - 533.0) <= 1.0);
    // 1x would use the space poorly (factor 1.94 possible): fractional.
    r = fitFrame(QSizeF(256, 384), QSizeF(1280, 744), true);
    QCOMPARE(r.height(), 744.0);
    QVERIFY(std::abs(r.width() / r.height() - 256.0 / 384.0) < 0.005);
    // Too small for 1x: fractional (aspect ratio preserved).
    r = fitFrame(QSizeF(256, 384), QSizeF(128, 400), true);
    QCOMPARE(r.width(), 128.0);
    QCOMPARE(r.height(), 192.0);
    QVERIFY(fitFrame(QSizeF(), QSizeF(10, 10), true).isEmpty());
  }

  void touchMapping() {
    const auto p = ndsProfile();
    // Frame 256x384 at 2x, top left at (10, 20).
    const QRectF frame(10, 20, 512, 768);
    // Upper screen: no touch.
    QVERIFY(!touchToFrame(p, frame, QPointF(100, 100), false).has_value());
    // Center of the lower screen (y = 20 + 384 + 192 = 596).
    auto pt = touchToFrame(p, frame, QPointF(10 + 256, 596), false);
    QVERIFY(pt.has_value());
    QVERIFY(std::abs(pt->x() - 0.5) < 1e-9);
    QVERIFY(std::abs(pt->y() - 0.75) < 1e-9);  // lower half: (192 + 96) / 384
    // Corners of the touch screen.
    pt = touchToFrame(p, frame, QPointF(10, 20 + 384), false);
    QVERIFY(pt.has_value());
    QVERIFY(std::abs(pt->x() - 0.0) < 1e-9 && std::abs(pt->y() - 0.5) < 1e-9);
    pt = touchToFrame(p, frame, QPointF(10 + 512, 20 + 768), false);
    QVERIFY(pt.has_value());
    QVERIFY(std::abs(pt->x() - 1.0) < 1e-9 && std::abs(pt->y() - 1.0) < 1e-9);
    // Outside: nullopt without clamp, clamped to the edge with clamp.
    QVERIFY(!touchToFrame(p, frame, QPointF(5, 596), false).has_value());
    pt = touchToFrame(p, frame, QPointF(5, 900), true);
    QVERIFY(pt.has_value());
    QVERIFY(std::abs(pt->x() - 0.0) < 1e-9 && std::abs(pt->y() - 1.0) < 1e-9);
    // Without touch screen.
    emu::DisplayProfile single;
    single.layout = QStringLiteral("single");
    single.screens = {{QStringLiteral("main"), 256, 192, false}};
    QVERIFY(!touchToFrame(single, QRectF(0, 0, 256, 192), QPointF(1, 1), true).has_value());
  }

  void resamplerPassthrough() {
    LinearResampler r(48000, 48000);
    const QByteArray in = stereo({1, 2, 3, 4, 5, 6});
    QVERIFY(r.isPassthrough());
    QCOMPARE(r.process(in), in);
  }

  void resamplerLength() {
    LinearResampler r(32768, 48000);  // typical DS rate
    qint64 inFrames = 0;
    qint64 outFrames = 0;
    for (int i = 0; i < 100; ++i) {
      QList<qint16> chunk;
      for (int f = 0; f < 546; ++f) {  // ~1/60 s
        chunk << qint16(1000) << qint16(-1000);
      }
      inFrames += 546;
      outFrames += r.process(stereo(chunk)).size() / 4;
    }
    const double expected = static_cast<double>(inFrames) * 48000.0 / 32768.0;
    QVERIFY2(std::abs(static_cast<double>(outFrames) - expected) <= 3.0, qPrintable(QString::number(outFrames)));
  }

  void resamplerSeamlessAcrossChunks() {
    // Ramp: linearly interpolated and monotonic across chunk boundaries, without jumps.
    LinearResampler r(24000, 48000);
    QList<qint16> all;
    for (int c = 0; c < 4; ++c) {
      QList<qint16> chunk;
      for (int f = 0; f < 50; ++f) {
        const qint16 v = qint16((c * 50 + f) * 100);
        chunk << v << v;
      }
      const QByteArray out = r.process(stereo(chunk));
      const auto* s = reinterpret_cast<const qint16*>(out.constData());
      for (int i = 0; i < out.size() / 2; ++i) all << s[i];
    }
    QVERIFY(all.size() >= 2 * 195);
    for (int i = 2; i < all.size(); i += 2) {
      const int d = all.at(i) - all.at(i - 2);
      QVERIFY2(d >= 0 && d <= 100, qPrintable(QStringLiteral("Jump at %1: %2").arg(i / 2).arg(d)));
      QCOMPARE(all.at(i), all.at(i + 1));  // L == R
    }
  }

  void resamplerHandlesTinyChunks() {
    LinearResampler r(32000, 48000);
    QCOMPARE(r.process(QByteArray()).size(), 0);
    QCOMPARE(r.process(QByteArray(3, 0)).size(), 0);  // incomplete frame
    const QByteArray out = r.process(stereo({100, 100}));
    QVERIFY(out.size() % 4 == 0);
  }

  void modelRolesAndFilter() {
    LibraryModel m;
    QSignalSpy counts(&m, &LibraryModel::countChanged);
    const QString shaA(64, QLatin1Char('a')), shaB(64, QLatin1Char('b')), shaC(64, QLatin1Char('c')), shaD(64, QLatin1Char('d'));
    const QList<GameEntry> games{game(QStringLiteral("g1"), QStringLiteral("Lumen Drift"), shaA, 8 * 1024 * 1024),
                                 game(QStringLiteral("g2"), QStringLiteral("Paper Wizards"), shaB, 128LL * 1024 * 1024),
                                 game(QStringLiteral("g3"), QStringLiteral("Copper Courier"), shaC, 16 * 1024 * 1024),
                                 game(QStringLiteral("g4"), QStringLiteral("Orbit Gardens"), shaD, 16 * 1024 * 1024)};
    m.setGames(games, [&](const GameEntry& g) {
      if (g.id == QLatin1String("g1")) return st(RomState::Ready, g.romSize, g.romSize);
      if (g.id == QLatin1String("g3")) return st(RomState::HashMismatch);
      if (g.id == QLatin1String("g4")) return st(RomState::Validating);
      return st(RomState::DownloadNeeded, 0, g.romSize);
    });
    QCOMPARE(m.rowCount(), 4);
    QCOMPARE(m.totalCount(), 4);
    QCOMPARE(m.readyCount(), 1);
    // Default sort is Name A–Z: address rows by game id, not by input order.
    auto roleOf = [&](const char* id, int r) { return m.data(m.index(m.rowOfGame(QString::fromLatin1(id))), r); };
    auto role = [&](int row, int r) { return m.data(m.index(row), r); };
    QCOMPARE(roleOf("g1", LibraryModel::TitleRole).toString(), QStringLiteral("Lumen Drift"));
    QCOMPARE(roleOf("g1", LibraryModel::MonogramRole).toString(), QStringLiteral("LD"));
    QCOMPARE(roleOf("g1", LibraryModel::SystemRole).toString(), QStringLiteral("NDS"));
    QCOMPARE(roleOf("g1", LibraryModel::StatusTextRole).toString(), QStringLiteral("Ready"));
    QCOMPARE(roleOf("g1", LibraryModel::StatusToneRole).toString(), QStringLiteral("ok"));
    QCOMPARE(roleOf("g2", LibraryModel::StatusTextRole).toString(), QStringLiteral("Download needed · 128 MB"));
    QCOMPARE(roleOf("g3", LibraryModel::StatusTextRole).toString(), QStringLiteral("Hash mismatch · reload"));
    QCOMPARE(roleOf("g3", LibraryModel::StatusToneRole).toString(), QStringLiteral("error"));
    QCOMPARE(roleOf("g4", LibraryModel::StatusTextRole).toString(), QStringLiteral("Verifying…"));
    QCOMPARE(roleOf("g1", LibraryModel::GameIdRole).toString(), QStringLiteral("g1"));
    QCOMPARE(m.roleNames().value(LibraryModel::StateKindRole), QByteArray("stateKind"));

    // Download progress
    m.setStatus(shaB, st(RomState::Downloading, 32LL * 1024 * 1024, 128LL * 1024 * 1024));
    QCOMPARE(roleOf("g2", LibraryModel::StatusTextRole).toString(), QStringLiteral("Downloading 25 %"));
    QVERIFY(std::abs(roleOf("g2", LibraryModel::ProgressRole).toDouble() - 0.25) < 1e-9);

    // Unknown state (e.g. future enum value): robust, neutral
    m.setStatus(shaD, st(static_cast<RomState>(99)));
    QCOMPARE(roleOf("g4", LibraryModel::StatusTextRole).toString(), QStringLiteral("Verifying…"));
    QCOMPARE(roleOf("g4", LibraryModel::StatusToneRole).toString(), QStringLiteral("neutral"));

    // Text filter (title, case-insensitive)
    m.setFilterText(QStringLiteral("ar"));
    QCOMPARE(m.rowCount(), 2);  // Paper Wizards, Orbit Gardens
    QCOMPARE(m.totalCount(), 4);
    m.setFilterText(QStringLiteral("  WIZ "));
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(role(0, LibraryModel::GameIdRole).toString(), QStringLiteral("g2"));
    m.setFilterText(QStringLiteral("zzz"));
    QCOMPARE(m.rowCount(), 0);
    m.setFilterText(QString());
    QCOMPARE(m.rowCount(), 4);

    // Ready filter, and a status change alters visibility
    m.setReadyOnly(true);
    QCOMPARE(m.rowCount(), 1);
    m.setStatus(shaB, st(RomState::Ready, 1, 1));
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.readyCount(), 2);
    QCOMPARE(m.rowOfGame(QStringLiteral("g2")), 1);
    QCOMPARE(m.rowOfGame(QStringLiteral("g3")), -1);
    m.setReadyOnly(false);
    QCOMPARE(m.rowCount(), 4);
    QVERIFY(counts.count() > 0);
    QVERIFY(m.game(QStringLiteral("g3")).has_value());
    m.clear();
    QCOMPARE(m.rowCount(), 0);
  }

  void modelHelpers() {
    QCOMPARE(LibraryModel::formatSize(16 * 1024 * 1024), QStringLiteral("16 MB"));
    QCOMPARE(LibraryModel::formatSize(128LL * 1024 * 1024), QStringLiteral("128 MB"));
    QCOMPARE(LibraryModel::formatSize(512 * 1024), QStringLiteral("512 KB"));
    QCOMPARE(LibraryModel::monogram(QStringLiteral("Tide & Lantern")), QStringLiteral("TL"));
    QCOMPARE(LibraryModel::monogram(QStringLiteral("solo")), QStringLiteral("S"));
    QCOMPARE(LibraryModel::monogram(QString()), QStringLiteral("?"));
  }
};

QTEST_GUILESS_MAIN(InputTest)
#include "input_test.moc"
