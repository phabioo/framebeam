// SessionHost::streamTimestamp: wall-clock stamps backdated by the speed-up lateness, strictly increasing.
#include <QtTest>

#include "sessionhost.h"

using uzume::SessionHost;

class StreamTimestampTest : public QObject {
  Q_OBJECT
 private slots:
  void normalSpeedUnchanged() {
    QCOMPARE(SessionHost::streamTimestamp(5'000'000, 0, 0), qint64{5'000'000});
    QCOMPARE(SessionHost::streamTimestamp(21'000'000, 0, 5'000'000), qint64{21'000'000});
  }
  void backdatedToSlot() {
    QCOMPARE(SessionHost::streamTimestamp(22'000'000, 5'400'000, 5'000'000), qint64{16'600'000});
  }
  void alwaysAfterPrevious() {
    QCOMPARE(SessionHost::streamTimestamp(10'000'000, 9'000'000, 8'000'000), qint64{8'000'001});
    QCOMPARE(SessionHost::streamTimestamp(10'000'000, -5, 0), qint64{10'000'000});  // negative lateness ignored
  }
};

QTEST_APPLESS_MAIN(StreamTimestampTest)
#include "stream_timestamp_test.moc"
