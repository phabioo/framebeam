// framebeam_player_cli: developer tool against a real FrameBeam Hub (QtCore/QtNetwork only).
//   identify <address> [--dev]
//   pair <address> [--dev] [--accept-fingerprint] [games] [fetch-rom <sha256>] [wait-revoked]...
//     wait-revoked: reports READY-FOR-REVOKE, renews the token every second and ends (exit 0) as soon as the
//     hub has revoked the device (state NeedsPairing); exit 1 after 60 s without revocation (E2E script).
//   games | fetch-rom <sha256>      (uses the last connected profile; Linux: credential in memory only,
//                                    so use them there as follow-up commands of "pair")
// Options: --data-dir <path> (otherwise FRAMEBEAM_DATA_DIR / AppDataLocation).
// Exit codes: 0 ok, 1 error, 2 user action required (confirm fingerprint, approval).
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <memory>

#include "credentialstore.h"
#include "hubconnection.h"
#include "hublibrary.h"
#include "profilestore.h"
#include "romcache.h"
#include "romdownloader.h"

using namespace framebeam;

namespace {

QTextStream& out() {
  static QTextStream s(stdout);
  return s;
}
QTextStream& err() {
  static QTextStream s(stderr);
  return s;
}

class Runner : public QObject {
 public:
  Runner(const QStringList& args, QObject* parent) : QObject(parent), args_(args) {}

  int start() {
    QString dataDir;
    QStringList rest;
    for (qsizetype i = 0; i < args_.size(); ++i) {
      const QString& a = args_.at(i);
      if (a == QLatin1String("--dev")) {
        dev_ = true;
      } else if (a == QLatin1String("--accept-fingerprint")) {
        accept_ = true;
      } else if (a == QLatin1String("--data-dir") && i + 1 < args_.size()) {
        dataDir = args_.at(++i);
      } else {
        rest << a;
      }
    }
    if (rest.isEmpty()) {
      return usage();
    }
    command_ = rest.takeFirst();
    if (command_ == QLatin1String("identify") || command_ == QLatin1String("pair")) {
      if (rest.isEmpty()) {
        return usage();
      }
      address_ = rest.takeFirst();
    } else if (command_ != QLatin1String("games") && command_ != QLatin1String("fetch-rom")) {
      return usage();
    } else {
      rest.prepend(command_);
      command_ = QStringLiteral("standalone");
    }
    followUps_ = rest;

    profiles_ = std::make_unique<ProfileStore>(dataDir);
    credentials_ = createDefaultCredentialStore();
    conn_ = std::make_unique<HubConnection>(profiles_.get(), credentials_.get());
    library_ = std::make_unique<HubLibrary>(conn_.get());
    cache_ = std::make_unique<RomCache>(profiles_->romCacheDir());
    downloader_ = std::make_unique<RomDownloader>(conn_.get(), cache_.get());
    QObject::connect(conn_.get(), &HubConnection::stateChanged, this, [this](HubConnection::State s) { onState(s); });
    if (command_ == QLatin1String("standalone")) {
      const QString last = profiles_->lastHubId();
      if (last.isEmpty()) {
        err() << "No last connected hub. First run: pair <address>\n";
        return 2;
      }
      conn_->connectToProfile(last);
      if (conn_->state() == HubConnection::State::Disconnected) {
        err() << "Error: " << conn_->errorCode() << "\n";
        return 1;
      }
    } else {
      conn_->connectToAddress(address_, dev_);
    }
    return -1;
  }

 private:
  int usage() {
    err() << "Usage: framebeam_player_cli identify <address> [--dev]\n"
             "        framebeam_player_cli pair <address> [--dev] [--accept-fingerprint] [games] [fetch-rom <sha256>]\n"
             "        framebeam_player_cli games | fetch-rom <sha256>   [--data-dir <path>]\n";
    return 1;
  }

  void finish(int code) {
    QMetaObject::invokeMethod(qApp, [code]() { QCoreApplication::exit(code); }, Qt::QueuedConnection);
  }

  void printHub() {
    const HubInfo& i = conn_->hubInfo();
    out() << "Hub: " << i.name << " (" << i.hubId << ") Version " << i.hubVersion << " Protocol " << i.protocolVersion
          << " (min " << i.minProtocolVersion << ")\n";
    if (!conn_->observedFingerprint().isEmpty()) {
      out() << "Certificate fingerprint (SHA-256): " << conn_->observedFingerprint() << "\n";
    }
    out().flush();
  }

  void onState(HubConnection::State s) {
    using S = HubConnection::State;
    err() << "[" << HubConnection::stateName(s) << "]\n";
    err().flush();
    const bool identify = command_ == QLatin1String("identify");
    switch (s) {
      case S::NeedsTrustConfirmation:
        printHub();
        if (!identify && accept_) {
          conn_->confirmTrust();
        } else {
          out() << (identify ? "First contact: compare the fingerprint with the hub settings page.\n"
                             : "Compare the fingerprint with the hub settings page and confirm with --accept-fingerprint.\n");
          out().flush();
          finish(identify ? 0 : 2);
        }
        break;
      case S::CertificateChanged:
        out() << "CERTIFICATE CHANGED. Expected: " << conn_->expectedFingerprint() << "\nSeen:      "
              << conn_->observedFingerprint() << "\n";
        out().flush();
        finish(1);
        break;
      case S::Incompatible:
      case S::Unreachable:
      case S::Disconnected:
        out() << "Error: " << conn_->errorCode() << " " << conn_->errorMessage() << "\n";
        out().flush();
        finish(s == S::Disconnected ? 2 : 1);
        break;
      case S::NeedsPairing:
        if (revokeWatch_ != nullptr) {
          out() << "Revoked: " << conn_->errorCode() << " -> NeedsPairing\n";
          out().flush();
          finish(0);
        } else if (identify) {
          printHub();
          out() << "Hub reachable, pairing required.\n";
          out().flush();
          finish(0);
        } else if (command_ == QLatin1String("pair")) {
          conn_->requestPairing();
        } else {
          out() << "No credential available. Use 'pair <address> " << followUps_.join(QLatin1Char(' ')) << "'.\n";
          out().flush();
          finish(2);
        }
        break;
      case S::AwaitingApproval:
        out() << "Waiting for approval in the hub web interface (Allow/Deny) ...\n";
        out().flush();
        break;
      case S::Denied:
      case S::Expired:
        out() << (s == S::Denied ? "Pairing denied.\n" : "Pairing request expired.\n");
        out().flush();
        finish(2);
        break;
      case S::Connected:
        printHub();
        if (identify) {
          finish(0);
        } else {
          runFollowUps();
        }
        break;
      case S::Identifying:
      case S::Authenticating:
        break;
    }
  }

  void runFollowUps() {
    if (followUps_.isEmpty()) {
      out() << "Connected.\n";
      out().flush();
      finish(0);
      return;
    }
    QObject::connect(library_.get(), &HubLibrary::loadFailed, this, [this](const QString& c, const QString& m) {
      err() << "Library: " << c << " " << m << "\n";
      finish(1);
    });
    QObject::connect(library_.get(), &HubLibrary::loaded, this, [this]() { nextFollowUp(); }, Qt::SingleShotConnection);
    library_->reload();
  }

  void nextFollowUp() {
    if (followUps_.isEmpty()) {
      finish(0);
      return;
    }
    const QString cmd = followUps_.takeFirst();
    if (cmd == QLatin1String("games")) {
      for (const GameEntry& g : library_->games()) {
        out() << g.title << "\t" << g.system << "\t" << g.romSize << " B\t" << g.romSha256 << "\n";
      }
      out().flush();
      nextFollowUp();
    } else if (cmd == QLatin1String("wait-revoked")) {
      revokeWatch_ = new QTimer(this);
      revokeWatch_->setInterval(1000);
      int ticks = 0;
      QObject::connect(revokeWatch_, &QTimer::timeout, this, [this, ticks]() mutable {
        if (++ticks > 60) {
          err() << "No revocation within 60 s\n";
          finish(1);
          revokeWatch_->stop();
          return;
        }
        conn_->noteUnauthorized();  // renews the token; after revocation -> NeedsPairing
      });
      revokeWatch_->start();
      out() << "READY-FOR-REVOKE\n";
      out().flush();
    } else if (cmd == QLatin1String("fetch-rom") && !followUps_.isEmpty()) {
      const QString sha = followUps_.takeFirst().toLower();
      const auto game = library_->gameByRomSha(sha);
      if (!game) {
        err() << "ROM " << sha << " not in the library\n";
        finish(1);
        return;
      }
      downloader_->disconnect(this);
      lastPct_ = -1;
      QObject::connect(downloader_.get(), &RomDownloader::progress, this, [this](const QString&, qint64 got, qint64 total) {
        const int pct = total > 0 ? static_cast<int>(got * 100 / total) : 100;
        if (pct / 10 != lastPct_ / 10) {
          err() << "  " << pct << " %\n";
          err().flush();
        }
        lastPct_ = pct;
      });
      QObject::connect(downloader_.get(), &RomDownloader::statusChanged, this, [this](const QString&, const RomStatus& st) {
        if (st.state == RomState::Ready) {
          out() << "ROM ready: " << st.localPath << "\n";
          out().flush();
          nextFollowUp();
        } else if (st.state == RomState::HashMismatch || st.state == RomState::Failed) {
          err() << "ROM download failed: " << st.errorCode << " " << st.errorMessage << "\n";
          finish(1);
        }
      });
      downloader_->ensureRom(*game);
    } else {
      err() << "Unknown command: " << cmd << "\n";
      finish(1);
    }
  }

  QStringList args_;
  QString command_;
  QString address_;
  QStringList followUps_;
  QTimer* revokeWatch_ = nullptr;
  int lastPct_ = -1;
  bool dev_ = false;
  bool accept_ = false;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<CredentialStore> credentials_;
  std::unique_ptr<HubConnection> conn_;
  std::unique_ptr<HubLibrary> library_;
  std::unique_ptr<RomCache> cache_;
  std::unique_ptr<RomDownloader> downloader_;
};

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("FrameBeam"));
  QCoreApplication::setApplicationName(QStringLiteral("FrameBeam Player"));
  Runner runner(app.arguments().mid(1), &app);
  const int early = runner.start();
  if (early >= 0) {
    return early;
  }
  return app.exec();
}
