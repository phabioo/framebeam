#include "linkhandler.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalSocket>
#include <QSettings>

namespace uzume::app {

namespace {
constexpr int kConnectTimeoutMs = 500;
constexpr int kWriteTimeoutMs = 1000;
constexpr qsizetype kMaxMessage = 8192;
}  // namespace

QString findLinkArgument(const QStringList& arguments) {
  for (const QString& a : arguments) {
    if (a.startsWith(QLatin1String("uzume://"), Qt::CaseInsensitive)) return a;
  }
  return {};
}

QString instanceServerName(const QString& userName, const QString& dataDir) {
  const QByteArray key = userName.toUtf8() + '\n' + QDir::cleanPath(dataDir).toUtf8();
  const QString hash = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex().left(16));
  return QStringLiteral("uzume-instance-") + hash;
}

QString schemeOpenCommand(const QString& exePath) {
  return QStringLiteral("\"%1\" \"%2\"").arg(QDir::toNativeSeparators(exePath), QStringLiteral("%1"));
}

bool ensureSchemeRegistered(const QString& exePath) {
#ifdef Q_OS_WIN
  QSettings classes(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"), QSettings::NativeFormat);
  const QString command = schemeOpenCommand(exePath);
  const QString commandKey = QStringLiteral("uzume/shell/open/command/.");
  const bool inPlace = classes.value(commandKey).toString() == command &&
                       classes.contains(QStringLiteral("uzume/URL Protocol/."));
  if (!inPlace) {
    classes.setValue(QStringLiteral("uzume/."), QStringLiteral("URL:Uzume"));
    classes.setValue(QStringLiteral("uzume/URL Protocol"), QString());
    classes.setValue(QStringLiteral("uzume/DefaultIcon/."), QStringLiteral("\"%1\",0").arg(QDir::toNativeSeparators(exePath)));
    classes.setValue(commandKey, command);
    classes.sync();
  }
  return classes.status() == QSettings::NoError;
#else
  Q_UNUSED(exePath);
  return false;
#endif
}

SingleInstance::SingleInstance(const QString& serverName, QObject* parent) : QObject(parent), name_(serverName) {
  connect(&server_, &QLocalServer::newConnection, this, [this] {
    while (QLocalSocket* s = server_.nextPendingConnection()) {
      connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
      auto* buffer = new QByteArray;
      connect(s, &QObject::destroyed, s, [buffer] { delete buffer; });
      auto handle = [this, s, buffer] {
        buffer->append(s->readAll());
        if (buffer->size() > kMaxMessage) {
          s->abort();
          return;
        }
        const qsizetype nl = buffer->indexOf('\n');
        if (nl < 0) return;
        const QString link = QString::fromUtf8(buffer->left(nl)).trimmed();
        buffer->clear();
        s->write("ok\n");
        s->flush();
        s->disconnectFromServer();
        emit activateRequested();
        emit linkReceived(link);
      };
      connect(s, &QLocalSocket::readyRead, s, handle);
      handle();
    }
  });
}

SingleInstance::~SingleInstance() { server_.close(); }

bool SingleInstance::acquire(const QString& link) {
  {
    QLocalSocket probe;
    probe.connectToServer(name_);
    if (probe.waitForConnected(kConnectTimeoutMs)) {
      probe.write(link.toUtf8() + '\n');
      probe.waitForBytesWritten(kWriteTimeoutMs);
      probe.waitForReadyRead(kWriteTimeoutMs);  // the running instance answers before it acts
      return false;
    }
  }
  QLocalServer::removeServer(name_);  // a stale socket file of a crashed run (Unix)
  server_.setSocketOptions(QLocalServer::UserAccessOption);
  if (!server_.listen(name_)) {
    return true;  // no lock possible: run anyway, links only work through the command line then
  }
  return true;
}

}  // namespace uzume::app
