#pragma once
// `uzume://` links and the single running instance of the app (0.18 package 7, docs/design/uzume/frame.md 8c).
//   * A link is a positional command line argument. The second start of the app passes it to the running one over a local
//     socket (one per user and data directory) and exits.
//   * On Windows the app registers itself for the scheme under HKCU\Software\Classes\uzume.
// Not used in smoke or test mode.

#include <QLocalServer>
#include <QObject>
#include <QString>
#include <QStringList>

namespace uzume::app {

// First argument that is an `uzume://` link (scheme in any case), empty if none.
QString findLinkArgument(const QStringList& arguments);

// Name of the local socket: per user and per data directory (empty = the default directory), so a second app with its own
// --data-dir (tests, E2E scripts) is not mistaken for the first.
QString instanceServerName(const QString& userName, const QString& dataDir);

// Value of `shell\open\command`: "<exe>" "%1" with the native path of the executable.
QString schemeOpenCommand(const QString& exePath);

// Windows: HKCU\Software\Classes\uzume with URL Protocol and the open command of exePath, written when missing or pointing
// elsewhere. Returns true when the registration is in place afterwards (always false on other platforms).
bool ensureSchemeRegistered(const QString& exePath);

class SingleInstance : public QObject {
  Q_OBJECT
 public:
  explicit SingleInstance(const QString& serverName, QObject* parent = nullptr);
  ~SingleInstance() override;

  // True: this process is the instance (it listens). False: another instance answered; `link` (may be empty = just bring
  // the window up) was sent to it and this process should exit.
  bool acquire(const QString& link);

 signals:
  void linkReceived(const QString& link);  // may be empty: another start without a link
  void activateRequested();

 private:
  QString name_;
  QLocalServer server_;
};

}  // namespace uzume::app
