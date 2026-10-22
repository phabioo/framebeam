#include "uploadcontroller.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <utility>

#include "profilestore.h"

namespace uzume::ui {

UploadController::UploadController(const Deps& d, QObject* parent) : QObject(parent), d_(d) {
  uploader_ = std::make_unique<GameUploader>(d_.conn);
  upload_ = {{QStringLiteral("active"), false}, {QStringLiteral("fileName"), QString()}, {QStringLiteral("progress"), 0.0},
             {QStringLiteral("message"), QString()}, {QStringLiteral("isError"), false}};
  connect(uploader_.get(), &GameUploader::progress, this, [this](qint64 sent, qint64 total) {
    upload_.insert(QStringLiteral("progress"), total > 0 ? static_cast<double>(sent) / static_cast<double>(total) : 0.0);
    emit changed();
  });
  connect(uploader_.get(), &GameUploader::finished, this, &UploadController::onFinished);
  uploader_->setStateFileProvider([this]() {
    const QString dir = d_.profiles->hubDir(d_.conn->hubInfo().hubId);
    return dir.isEmpty() ? QString() : QDir(dir).filePath(QStringLiteral("uploads.json"));
  });
}

bool UploadController::canUpload() const {
  return d_.conn->state() == HubConnection::State::Connected && d_.conn->hubHasFeature(QStringLiteral("uploads_v1"));
}

QStringList UploadController::filters() const {
  QStringList exts;
  for (const emu::SystemManifest& m : d_.manifests.all()) {
    for (const QString& e : m.extensions) {
      exts.append(QStringLiteral("*") + e);
    }
  }
  QStringList filters;
  if (!exts.isEmpty()) {
    filters.append(tr("Game ROMs (%1)").arg(exts.join(QLatin1Char(' '))));
  }
  filters.append(tr("All files (*)"));
  return filters;
}

void UploadController::uploadRom(const QString& source) {
  if (!canUpload() || uploader_->busy()) {
    return;
  }
  const QString path = source.startsWith(QLatin1String("file:")) ? QUrl(source).toLocalFile() : source;
  upload_ = {{QStringLiteral("active"), true},
             {QStringLiteral("fileName"), QFileInfo(path).fileName()},
             {QStringLiteral("progress"), 0.0},
             {QStringLiteral("message"), QString()},
             {QStringLiteral("isError"), false}};
  sourcePath_ = path;
  emit changed();
  uploader_->upload(path);
}

void UploadController::dismissMessage() {
  if (upload_.value(QStringLiteral("active")).toBool() || upload_.value(QStringLiteral("message")).toString().isEmpty()) {
    return;
  }
  upload_.insert(QStringLiteral("message"), QString());
  emit changed();
}

QString UploadController::uploadLocalGame(const QString& gameId) {
  const auto game = d_.model.game(gameId);
  if (!game || game->onHub || !canUpload() || uploader_->busy()) return {};
  LocalRomFile local;
  for (const RomFileEntry& r : game->roms) {
    const QList<LocalRomFile> files = d_.localRoms->filesFor(r.sha256);
    if (!files.isEmpty() && !d_.localRoms->pathForSha(r.sha256).isEmpty()) {
      local = files.first();
      break;
    }
  }
  if (local.path.isEmpty()) {
    return tr("File missing on this device. The game cannot be uploaded from here.");
  }
  upload_ = {{QStringLiteral("active"), true},
             {QStringLiteral("gameId"), gameId},
             {QStringLiteral("fileName"), QFileInfo(local.path).fileName()},
             {QStringLiteral("progress"), 0.0},
             {QStringLiteral("message"), QString()},
             {QStringLiteral("isError"), false}};
  sourcePath_ = local.path;
  emit changed();
  uploader_->upload(local.path, QString(), local.sha256, local.system);
  return {};
}

void UploadController::onFinished(const UploadResult& r) {
  using K = UploadResult::Kind;
  const QString srcPath = std::exchange(sourcePath_, QString());
  emit finished(r, srcPath);  // library side effects (select, reload, ROM cache) first, as before
  QString message;
  bool isError = true;
  switch (r.kind) {
    case K::Created:
      message = tr("Uploaded “%1” to the server.").arg(r.game.title);
      isError = false;
      break;
    case K::Duplicate:
      message = tr("Already in the library.");
      isError = false;
      break;
    case K::Forbidden:
      message = tr("Uploads are not allowed for your user on this server.");
      break;
    case K::TooLarge:
      message = tr("The file is too large for this server.");
      break;
    case K::Rejected:
      message = r.errorMessage.isEmpty() ? tr("The server rejected this file (unsupported type?).")
                                         : tr("The server rejected this file: %1").arg(r.errorMessage);
      break;
    case K::Failed:
      if (r.errorCode == QLatin1String("file_unreadable")) {
        message = tr("The file cannot be read.");
      } else if (r.errorCode == QLatin1String("cancelled")) {
        message = tr("Upload cancelled.");
      } else if (r.errorCode == QLatin1String("not_connected")) {
        message = tr("Not connected to a server.");
      } else {
        message = tr("Upload failed: %1").arg(r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage);
      }
      break;
  }
  upload_ = {{QStringLiteral("active"), false},
             {QStringLiteral("fileName"), r.fileName},
             {QStringLiteral("progress"), r.kind == K::Created ? 1.0 : 0.0},
             {QStringLiteral("message"), message},
             {QStringLiteral("isError"), isError}};
  emit changed();
}

}  // namespace uzume::ui
