#pragma once
// PersonalController: favourites and collections of the signed-in person (Uzume 0.19, design 3c-0, 3c-1, 3c-4). Loads the
// collections of the active server into the LibraryModel and sends the changes (favourite toggle, create / rename / delete a
// collection, add / remove a game). Changes show at once in the model and are taken back when the server refuses them. An
// older server without collections leaves everything hidden (`supported` false).
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include "hubconnection.h"
#include "librarymodel.h"
#include "personalapi.h"

namespace uzume::ui {

class PersonalController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")
  Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
 public:
  PersonalController(HubConnection* connection, LibraryModel* model, QObject* parent = nullptr);

  bool supported() const { return model_->personalSupported(); }
  bool busy() const { return pending_ > 0; }

  // Reloads the collections of the person (after the Library was loaded).
  void refresh();
  void clear();

  Q_INVOKABLE void toggleFavorite(const QString& gameId);
  // gameId: a game to put into the new collection at once (the "New collection" step of "Add to collection"), empty = none.
  Q_INVOKABLE void createCollection(const QString& name, const QString& gameId = {});
  Q_INVOKABLE void renameCollection(const QString& collectionId, const QString& name);
  Q_INVOKABLE void deleteCollection(const QString& collectionId);
  Q_INVOKABLE void setInCollection(const QString& collectionId, const QString& gameId, bool member);
  // Name of a collection ("" for an unknown id).
  Q_INVOKABLE QString collectionName(const QString& collectionId) const;
  // Local check of a collection name: "" = fine, else the sentence to show.
  Q_INVOKABLE QString nameProblem(const QString& name, const QString& exceptId = {}) const;

 signals:
  void supportedChanged();
  void busyChanged();
  // One request is done: op = "favorite" | "create" | "rename" | "delete" | "member"; error is a sentence for the user.
  void finished(const QString& op, bool ok, const QString& error);

 private:
  QString errorText(const PersonalResult& r) const;
  void begin();
  void end();

  QPointer<HubConnection> conn_;
  LibraryModel* model_;
  PersonalApi api_;
  int pending_ = 0;
  quint64 generation_ = 0;
};

}  // namespace uzume::ui
