#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

#include "hubconnection.h"

namespace uzume {

// A collection of the signed-in person (Uzume 0.19 `GET /collections`): their own named list of games.
struct CollectionInfo {
  QString id;
  QString name;
  QStringList gameIds;
  QString createdAt;
};
std::optional<CollectionInfo> parseCollection(const QJsonObject& obj);

struct PersonalResult {
  bool ok = false;
  int status = 0;
  // "" on success, else the server's code (name_taken for 409, bad_request, not_found, ...), "unreachable" without an
  // answer, "not_connected" when no request could be made.
  QString errorCode;
  QString errorMessage;
  QList<CollectionInfo> collections;  // listCollections
  CollectionInfo collection;          // createCollection, renameCollection
  bool nameTaken() const { return status == 409; }
};

// Favourites and collections of the calling person on the connected server. The callback runs on the event loop, never
// synchronously, and is dropped if the PersonalApi is destroyed.
class PersonalApi : public QObject {
  Q_OBJECT
 public:
  using Callback = std::function<void(const PersonalResult&)>;
  explicit PersonalApi(HubConnection* connection, QObject* parent = nullptr);

  void listCollections(Callback cb);                                                    // GET /collections
  void setFavorite(const QString& gameId, bool favorite, Callback cb);                  // PUT / DELETE /games/{id}/favorite
  void createCollection(const QString& name, const QStringList& gameIds, Callback cb);  // POST /collections
  void renameCollection(const QString& id, const QString& name, Callback cb);           // PATCH /collections/{id}
  void deleteCollection(const QString& id, Callback cb);                                // DELETE /collections/{id}
  void setInCollection(const QString& id, const QString& gameId, bool member, Callback cb);  // PUT / DELETE .../games/{game}

  static constexpr int kMaxNameLength = 40;

 private:
  enum class Expect { None, List, One };
  void request(const QByteArray& method, const QString& path, const QJsonObject& body, bool hasBody, Expect expect, Callback cb);

  QPointer<HubConnection> conn_;
};

}  // namespace uzume
