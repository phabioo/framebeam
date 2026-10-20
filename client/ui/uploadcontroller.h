#pragma once
// UploadController: ROM upload state of the Player (ADR 0017 D3 rest). Owns the GameUploader and the `upload` map
// (active, fileName, progress, message, isError) that PlayerController exposes to QML unchanged. Library side effects of
// a finished upload (select, reload, ROM cache adoption) stay in PlayerController: it handles `finished`.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <memory>

#include "gameuploader.h"
#include "hubconnection.h"
#include "librarymodel.h"
#include "localromindex.h"
#include "system_manifest.h"

namespace framebeam::ui {

class UploadController : public QObject {
  Q_OBJECT

 public:
  struct Deps {
    HubConnection* conn;
    ProfileStore* profiles;
    LibraryModel& model;
    const std::unique_ptr<LocalRomIndex>& localRoms;
    const emu::ManifestRegistry& manifests;
  };

  explicit UploadController(const Deps& d, QObject* parent = nullptr);

  GameUploader* uploader() { return uploader_.get(); }
  bool canUpload() const;  // handshake feature uploads_v1
  QVariantMap upload() const { return upload_; }
  QStringList filters() const;

  // Source = local path or file:// URL. No-op without uploads_v1 or while an upload runs.
  void uploadRom(const QString& source);
  void dismissMessage();
  // Uploads the local file of a local-only game; returns an error text when nothing was started because the file is gone.
  QString uploadLocalGame(const QString& gameId);

 signals:
  void changed();
  // Emitted before `upload` shows the result; srcPath is the local file that was uploaded.
  void finished(const framebeam::UploadResult& result, const QString& srcPath);

 private:
  void onFinished(const UploadResult& r);

  Deps d_;
  std::unique_ptr<GameUploader> uploader_;
  QVariantMap upload_;
  QString sourcePath_;
};

}  // namespace framebeam::ui
