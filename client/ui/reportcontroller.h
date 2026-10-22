#pragma once
// ReportController: QML face of "Report a problem" (docs/design/player.md 6d). Owns the ProblemReport (built off the GUI
// thread when the dialog opens) and the ReportUploader; PlayerController exposes it as `report`. The dialog is
// ReportProblemDialog.qml, hosted in Main.qml and opened from Settings -> Diagnostics and the in-game "⋯" menu.
//
// state: preparing (attachments are being collected) | editing | sending | sent | offline | failed

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

#include "hubconnection.h"
#include "problemreport.h"
#include "reportuploader.h"

namespace framebeam::ui {

class ReportController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by PlayerController")

  Q_PROPERTY(bool open READ isOpen NOTIFY changed)
  Q_PROPERTY(QString state READ state NOTIFY changed)
  Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
  Q_PROPERTY(int maxDescription READ maxDescription CONSTANT)
  Q_PROPERTY(QString hubName READ hubName NOTIFY changed)
  Q_PROPERTY(QString linkedText READ linkedText NOTIFY changed)  // empty without a linked event
  Q_PROPERTY(QVariantList attachments READ attachments NOTIFY changed)  // [{id, label, detail, sizeText, checked}]
  Q_PROPERTY(QString totalText READ totalText NOTIFY changed)            // "2.4 MB" of the checked attachments
  Q_PROPERTY(QStringList includedFiles READ includedFiles NOTIFY changed)
  Q_PROPERTY(bool canSend READ canSend NOTIFY changed)
  Q_PROPERTY(QString blockedText READ blockedText NOTIFY changed)  // why Send is not possible (empty = no reason to show)
  Q_PROPERTY(double progress READ progress NOTIFY progressChanged)  // 0..1 while sending
  Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)  // "1.0 of 2.4 MB"
  Q_PROPERTY(qint64 reportId READ reportId NOTIFY changed)
  Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
  Q_PROPERTY(QString saveNote READ saveNote NOTIFY changed)  // result of "Save as file…"

 public:
  struct Deps {
    HubConnection* conn = nullptr;
    QString dataDir;
    // The last connection check result, if any: {json, time, marked}. marked = "Add to problem report" was pressed, the
    // attachment then starts checked. sent() is called once a report went out (ends the mark).
    struct CheckInfo {
      QByteArray json;
      QDateTime at;
      bool marked = false;
    };
    std::function<CheckInfo()> lastCheck;
    std::function<void()> sent;
    std::function<QJsonObject()> systemInfo;  // diag::offlineInfo()-shaped (PlayerController::diagnosticsInfo)
  };
  explicit ReportController(const Deps& deps, QObject* parent = nullptr);
  ~ReportController() override;

  bool isOpen() const { return open_; }
  QString state() const;
  QString description() const { return description_; }
  void setDescription(const QString& text);
  int maxDescription() const { return ProblemReport::kMaxDescription; }
  QString hubName() const;
  QString linkedText() const;
  QVariantList attachments() const;
  QString totalText() const;
  QStringList includedFiles() const;
  bool canSend() const;
  QString blockedText() const;
  double progress() const;
  QString progressText() const;
  qint64 reportId() const { return uploader_.reportId(); }
  QString errorText() const { return uploader_.errorText(); }
  QString saveNote() const { return saveNote_; }
  ReportUploader* uploader() { return &uploader_; }

  // Opens the dialog (empty text, attachments collected in the background). The linked event is optional: an error
  // message or banner passes its title and time.
  Q_INVOKABLE void show();
  Q_INVOKABLE void showLinked(const QString& title, qint64 whenMsecs);
  Q_INVOKABLE void close();  // Cancel / Done / Esc: aborts a running upload and forgets the text
  Q_INVOKABLE void setChecked(const QString& id, bool checked);
  Q_INVOKABLE void send();    // Ctrl+Enter; Try again after a failure
  Q_INVOKABLE void cancelSending();  // "Cancel" of the sending state: back to editing, the text is kept
  Q_INVOKABLE QString suggestedFileName() const;
  Q_INVOKABLE QString folderUrl() const;  // Desktop or Downloads (file URL) for the save dialog
  Q_INVOKABLE void saveAsFile(const QString& pathOrUrl);

  // Result of a connection check (another change adds the check), kept for the next report and attached as
  // "Connection check from <time>". An empty document forgets it.
  Q_INVOKABLE void setConnectionCheckJson(const QByteArray& json, const QDateTime& when);

 signals:
  void changed();
  void descriptionChanged();
  void progressChanged();
  void saveFinished(bool ok, const QString& path, const QString& message);

 private:
  void collect();
  void onUploaderState();
  void applyPending();
  QString sizeText(qint64 bytes) const;

  Deps d_;
  ReportUploader uploader_;
  ProblemReport report_;
  bool open_ = false;
  bool ready_ = false;   // report_ is collected
  bool saving_ = false;
  bool sentNoted_ = false;
  QString description_, linkedTitle_, saveNote_;
  QDateTime linkedTime_;
  QByteArray checkJson_;
  QDateTime checkTime_;
  int generation_ = 0;   // a late collect() result of a closed dialog is dropped
};

}  // namespace framebeam::ui
