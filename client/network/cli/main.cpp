// framebeam_player_cli: developer tool against a real FrameBeam Hub (QtCore/QtNetwork only).
//   identify <address> [--dev]
//   pair <address> [--dev] [--accept-fingerprint] [games] [fetch-rom <sha256>] [wait-revoked]...
//     wait-revoked: reports READY-FOR-REVOKE, renews the token every second and ends (exit 0) as soon as the
//     hub has revoked the device (state NeedsPairing); exit 1 after 60 s without revocation (E2E script).
//   saves list | save push <game_id> <file> [--base N] [--reason checkpoint|final|final_session_end]
//   save pull <game_id> <out> | save resolve <game_id> <conflict_id> use_hub|use_local --expected N
//     (slot "default"; output lines OK/CONFLICT/STALE/ERROR; exit 0 ok, 1 error, 3 upload conflict, 4 stale resolve)
//   session-share [--game <id>] [--visibility private|hub_users|invite_only] [--synthetic] [--seconds N]
//   session-watch (--session <id> | --first) [--seconds N]      (Sessions: ADR 0006; follow-up commands of "pair" like
//     the others; share publishes a Session with synthetic frames/tone, watch exits 0 with >= 30 frames and audio)
//   games | fetch-rom <sha256>      (uses the last connected profile; Linux: credential in memory only,
//                                    so use them there as follow-up commands of "pair")
// Options: --data-dir <path> (otherwise FRAMEBEAM_DATA_DIR / AppDataLocation).
// Exit codes: 0 ok, 1 error, 2 user action required (confirm fingerprint, approval).
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <memory>

#include "credentialstore.h"
#include "hubconnection.h"
#include "hublibrary.h"
#include "profilestore.h"
#include "romcache.h"
#include "romdownloader.h"
#include "saveapi.h"
#include "mediacaps.h"
#include "sessioncommands.h"

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
    } else if (command_ != QLatin1String("games") && command_ != QLatin1String("fetch-rom") &&
               command_ != QLatin1String("saves") && command_ != QLatin1String("save") &&
               command_ != QLatin1String("session-share") && command_ != QLatin1String("session-watch")) {
      return usage();
    } else {
      rest.prepend(command_);
      command_ = QStringLiteral("standalone");
    }
    followUps_ = rest;

    profiles_ = std::make_unique<ProfileStore>(dataDir);
    credentials_ = createDefaultCredentialStore();
    conn_ = std::make_unique<HubConnection>(profiles_.get(), credentials_.get());
    HandshakeInfo hs = HandshakeInfo::detect();
    applyMediaCapabilities(&hs);  // h264_encode/h264_decode/encoders from what libavcodec can really open
    conn_->setHandshakeInfo(hs);
    library_ = std::make_unique<HubLibrary>(conn_.get());
    cache_ = std::make_unique<RomCache>(profiles_->romCacheDir());
    downloader_ = std::make_unique<RomDownloader>(conn_.get(), cache_.get());
    saves_ = std::make_unique<SaveApi>(conn_.get());
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
             "        framebeam_player_cli games | fetch-rom <sha256>   [--data-dir <path>]\n"
             "        framebeam_player_cli saves list | save push <game_id> <file> [--base N] | save pull <game_id> <out>\n"
             "        framebeam_player_cli session-share [--game <id>] [--visibility V] --synthetic [--seconds N]\n"
             "        framebeam_player_cli session-watch (--session <id> | --first) [--seconds N]\n"
             "                             | save resolve <game_id> <conflict_id> use_hub|use_local --expected N\n";
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
        out() << g.title << "\t" << g.system << "\t" << g.romSize << " B\t" << g.romSha256 << "\t" << g.id << "\n";
      }
      out().flush();
      nextFollowUp();
    } else if (cmd == QLatin1String("saves") || cmd == QLatin1String("save")) {
      runSaveCommand(cmd);
    } else if (cmd == QLatin1String("session-share") || cmd == QLatin1String("session-watch")) {
      // Options up to the next known command belong to this one; the command runs until its end (exit code).
      QStringList opts;
      while (!followUps_.isEmpty() && followUps_.first().startsWith(QLatin1String("--"))) {
        opts << followUps_.takeFirst();
        if (!followUps_.isEmpty() && !followUps_.first().startsWith(QLatin1String("--")) && opts.last() != QLatin1String("--synthetic") &&
            opts.last() != QLatin1String("--first")) {
          opts << followUps_.takeFirst();
        }
      }
      sessions_ = std::make_unique<SessionCommands>(conn_.get(), library_.get(), [this](int code) { finish(code); });
      if (!(cmd == QLatin1String("session-share") ? sessions_->share(opts) : sessions_->watch(opts))) {
        finish(1);
      }
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

  void runSaveCommand(const QString& cmd) {
    const QString sub = followUps_.isEmpty() ? QString() : followUps_.takeFirst();
    // Options belong to the command that precedes them (several save commands can follow each other).
    int base_ = 0, expected_ = 0;
    QString reason_ = QStringLiteral("checkpoint");
    const qsizetype npos = sub == QLatin1String("resolve") ? 3 : (sub == QLatin1String("list") ? 0 : 2);
    while (followUps_.size() >= npos + 2) {
      const QString o = followUps_.at(npos);
      if (o == QLatin1String("--base")) base_ = followUps_.at(npos + 1).toInt();
      else if (o == QLatin1String("--expected")) expected_ = followUps_.at(npos + 1).toInt();
      else if (o == QLatin1String("--reason")) reason_ = followUps_.at(npos + 1);
      else break;
      followUps_.remove(npos, 2);
    }
    using K = SaveApiResult::Kind;
    const auto report = [this](const SaveApiResult& r, const QString& what) {
      if (r.kind == K::Ok) {
        return true;
      }
      if (r.kind == K::Conflict && r.conflict) {
        out() << "CONFLICT id=" << r.conflict->id << " hub_revision=" << r.conflict->hubRevision
              << " secured_version=" << r.conflict->securedVersion << " base=" << r.conflict->securedBaseRevision << "\n";
        out().flush();
        finish(3);
      } else if (r.kind == K::Stale) {
        out() << "STALE " << r.errorCode << "\n";
        out().flush();
        finish(4);
      } else {
        err() << "ERROR " << what << ": " << r.errorCode << " (HTTP " << r.status << ") " << r.errorMessage << "\n";
        finish(1);
      }
      return false;
    };
    if (cmd == QLatin1String("saves") && sub == QLatin1String("list")) {
      saves_->listSlots([this, report](const SaveApiResult& r) {
        if (!report(r, QStringLiteral("list"))) {
          return;
        }
        for (const SaveSlotInfo& s : r.slotList) {
          out() << s.gameId << "\t" << s.slot << "\trev=" << s.current.revision << "\t" << s.current.sha256 << "\t"
                << s.current.deviceName << "\topen_conflicts=" << s.openConflictCount << "\n";
        }
        out().flush();
        nextFollowUp();
      });
    } else if (cmd == QLatin1String("save") && sub == QLatin1String("push") && followUps_.size() >= 2) {
      const QString gameId = followUps_.takeFirst();
      QFile f(followUps_.takeFirst());
      if (!f.open(QIODevice::ReadOnly)) {
        err() << "ERROR cannot read file\n";
        finish(1);
        return;
      }
      saves_->putSave(gameId, QStringLiteral("default"), f.readAll(), base_, reason_, [this, report](const SaveApiResult& r) {
        if (!report(r, QStringLiteral("push"))) {
          return;
        }
        out() << "OK revision=" << r.slot->current.revision << " sha256=" << r.slot->current.sha256 << "\n";
        out().flush();
        nextFollowUp();
      });
    } else if (cmd == QLatin1String("save") && sub == QLatin1String("pull") && followUps_.size() >= 2) {
      const QString gameId = followUps_.takeFirst();
      const QString path = followUps_.takeFirst();
      saves_->getContent(gameId, QStringLiteral("default"), [this, report, path](const SaveApiResult& r) {
        if (!report(r, QStringLiteral("pull"))) {
          return;
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(r.content) != r.content.size()) {
          err() << "ERROR cannot write file\n";
          finish(1);
          return;
        }
        out() << "OK revision=" << r.contentRevision << " bytes=" << r.content.size() << "\n";
        out().flush();
        nextFollowUp();
      });
    } else if (cmd == QLatin1String("save") && sub == QLatin1String("resolve") && followUps_.size() >= 3 && expected_ > 0) {
      const QString gameId = followUps_.takeFirst();
      const QString conflictId = followUps_.takeFirst();
      const QString resolution = followUps_.takeFirst();
      saves_->resolve(gameId, QStringLiteral("default"), conflictId, resolution, expected_, [this, report](const SaveApiResult& r) {
        if (!report(r, QStringLiteral("resolve"))) {
          return;
        }
        out() << "OK revision=" << r.slot->current.revision << " sha256=" << r.slot->current.sha256 << "\n";
        out().flush();
        nextFollowUp();
      });
    } else {
      usage();
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
  std::unique_ptr<SaveApi> saves_;
  std::unique_ptr<SessionCommands> sessions_;
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
