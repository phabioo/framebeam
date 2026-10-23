#pragma once
// ScreenshotController (Uzume 0.19, design 3c-1 "Screenshots (N)" and the "Take screenshot" hotkey). Takes the picture of the
// running game, keeps it in <data>/screenshots/pending until the server has it, and lists the person's screenshots of the selected
// game for the game page: a strip of thumbnails with a viewer (previous / next, Save as..., Delete...). Nothing is lost silently: a
// screenshot that could not be uploaded stays on this PC and goes up with the next connection to the same server and person.
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <functional>

#include "hubconnection.h"
#include "profilestore.h"
#include "screenshotapi.h"
#include "screenshotimageprovider.h"
#include "screenshotqueue.h"

namespace uzume::ui {

class ScreenshotController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")
  // The server knows screenshots (the list answered); false on an older server: the section stays hidden.
  Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
  // The own screenshots of `gameId`, newest first: [{id, createdAt, dateText, whenText, runId, runName, width, height, sizeText}].
  Q_PROPERTY(QVariantList shots READ shots NOTIFY shotsChanged)
  Q_PROPERTY(int count READ count NOTIFY shotsChanged)
  Q_PROPERTY(QString gameId READ gameId NOTIFY shotsChanged)
  Q_PROPERTY(int pendingCount READ pendingCount NOTIFY pendingChanged)  // taken here, not on the server yet
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
 public:
  using FrameCallback = std::function<void(const QImage& frame, const QString& error)>;
  using FrameSource = std::function<void(FrameCallback)>;

  ScreenshotController(HubConnection* connection, ProfileStore* profiles, QObject* parent = nullptr);

  bool supported() const { return supported_; }
  QVariantList shots() const { return shots_; }
  int count() const { return static_cast<int>(shots_.size()); }
  QString gameId() const { return gameId_; }
  int pendingCount() const;
  bool busy() const { return pending_ > 0; }
  ScreenshotCache* cache() { return &cache_; }
  ScreenshotQueue* queue() { return &queue_; }

  // The running game: its id and run (empty = not in a run) and the picture of its screen. Set by the PlayerController.
  void setGameSource(std::function<QString()> gameId, std::function<QString()> runId, FrameSource frames);
  // Name of a run for the viewer ("" = unknown).
  void setRunNameLookup(std::function<QString(const QString&)> lookup) { runName_ = std::move(lookup); }

  // Hotkey: takes the picture of the running game and sends it. Does nothing without a running game.
  Q_INVOKABLE void capture();
  // Stores and sends a picture (capture() with the frame at hand; used by the tests).
  void submit(const QImage& frame, const QString& gameId, const QString& runId);
  // Lists the screenshots of a game for the game page (empty id = none shown).
  Q_INVOKABLE void loadGame(const QString& gameId);
  Q_INVOKABLE void remove(const QString& screenshotId);
  // Writes the PNG of a screenshot to a file the person chose (file URL or path); finished("save", ...) follows.
  Q_INVOKABLE void saveAs(const QString& screenshotId, const QString& target);
  Q_INVOKABLE QString suggestedFileName(const QString& screenshotId) const;
  // Folder shown first in "Save as..." (file URL), "" = the dialog's default.
  Q_INVOKABLE QString exportFolderUrl() const;
  void clear();
  // Tries the screenshots waiting for this server and person again (a connection came back, the app started).
  void retryPending();

 signals:
  void supportedChanged();
  void shotsChanged();
  void pendingChanged();
  void busyChanged();
  // One line for the game: "Screenshot saved" or the offline variant.
  void notice(const QString& text);
  // op = "remove" | "save"; error is a sentence for the user.
  void finished(const QString& op, bool ok, const QString& error);

 private:
  QVariantMap rowFor(const ScreenshotInfo& s) const;
  void refreshList();
  void onUploaded(const ScreenshotInfo& info);
  void begin();
  void end();
  // Server and person the screenshots belong to: the connected ones, else those of the last connection (offline in a game).
  void identity(QString* hubId, QString* userId) const;

  QPointer<HubConnection> conn_;
  ProfileStore* profiles_;
  ScreenshotApi api_;
  ScreenshotQueue queue_;
  ScreenshotCache cache_;
  std::function<QString()> currentGame_;
  std::function<QString()> currentRun_;
  FrameSource frames_;
  std::function<QString(const QString&)> runName_;
  bool supported_ = false;
  QString gameId_;
  QList<ScreenshotInfo> infos_;
  QVariantList shots_;
  quint64 generation_ = 0;
  int pending_ = 0;
  QStringList fresh_;  // captures whose result decides the notice
};

}  // namespace uzume::ui
