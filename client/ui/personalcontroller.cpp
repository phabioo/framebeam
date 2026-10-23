#include "personalcontroller.h"

namespace uzume::ui {

PersonalController::PersonalController(HubConnection* connection, LibraryModel* model, QObject* parent)
    : QObject(parent), conn_(connection), model_(model), api_(connection, this) {
  connect(model_, &LibraryModel::collectionsChanged, this, &PersonalController::supportedChanged);
}

void PersonalController::begin() {
  if (pending_++ == 0) emit busyChanged();
}

void PersonalController::end() {
  if (pending_ > 0 && --pending_ == 0) emit busyChanged();
}

QString PersonalController::errorText(const PersonalResult& r) const {
  if (r.errorCode == QLatin1String("unreachable") || r.errorCode == QLatin1String("not_connected")) {
    return tr("The server could not be reached. Nothing was changed.");
  }
  if (r.status == 409) return tr("You already have a collection with that name.");
  if (r.status == 404) return tr("That game or collection no longer exists.");
  if (r.status == 400) return r.errorMessage.isEmpty() ? tr("The server did not accept that name.") : r.errorMessage;
  return r.errorMessage.isEmpty() ? tr("The server refused the change.") : r.errorMessage;
}

void PersonalController::clear() {
  ++generation_;
  pending_ = 0;
}

void PersonalController::refresh() {
  const quint64 gen = ++generation_;
  api_.listCollections([this, gen](const PersonalResult& r) {
    if (gen != generation_ || !r.ok) {
      return;  // an older server answers 404: the collection row stays hidden
    }
    model_->setCollections(r.collections);
  });
}

QString PersonalController::collectionName(const QString& collectionId) const {
  for (const CollectionInfo& c : model_->collections()) {
    if (c.id == collectionId) return c.name;
  }
  return {};
}

QString PersonalController::nameProblem(const QString& name, const QString& exceptId) const {
  const QString n = name.trimmed();
  if (n.isEmpty()) return tr("Give the collection a name.");
  if (n.size() > PersonalApi::kMaxNameLength) return tr("A name has at most %1 characters.").arg(PersonalApi::kMaxNameLength);
  for (const CollectionInfo& c : model_->collections()) {
    if (c.id != exceptId && c.name.compare(n, Qt::CaseInsensitive) == 0) return tr("You already have a collection with that name.");
  }
  return {};
}

void PersonalController::toggleFavorite(const QString& gameId) {
  const QVariantMap p = model_->personalFor(gameId);
  if (p.isEmpty()) return;
  const bool target = !p.value(QStringLiteral("favorite")).toBool();
  model_->setFavorite(gameId, target);
  begin();
  api_.setFavorite(gameId, target, [this, gameId, target](const PersonalResult& r) {
    end();
    if (!r.ok) {
      model_->setFavorite(gameId, !target);
    }
    emit finished(QStringLiteral("favorite"), r.ok, r.ok ? QString() : errorText(r));
  });
}

void PersonalController::createCollection(const QString& name, const QString& gameId) {
  if (const QString problem = nameProblem(name); !problem.isEmpty()) {
    emit finished(QStringLiteral("create"), false, problem);
    return;
  }
  begin();
  api_.createCollection(name, gameId.isEmpty() ? QStringList() : QStringList{gameId}, [this](const PersonalResult& r) {
    end();
    if (r.ok) {
      QList<CollectionInfo> all = model_->collections();
      all.append(r.collection);
      model_->setCollections(all);
    }
    emit finished(QStringLiteral("create"), r.ok, r.ok ? QString() : errorText(r));
  });
}

void PersonalController::renameCollection(const QString& collectionId, const QString& name) {
  if (const QString problem = nameProblem(name, collectionId); !problem.isEmpty()) {
    emit finished(QStringLiteral("rename"), false, problem);
    return;
  }
  begin();
  api_.renameCollection(collectionId, name, [this, collectionId](const PersonalResult& r) {
    end();
    if (r.ok) {
      QList<CollectionInfo> all = model_->collections();
      for (CollectionInfo& c : all) {
        if (c.id == collectionId) c.name = r.collection.name;
      }
      model_->setCollections(all);
    }
    emit finished(QStringLiteral("rename"), r.ok, r.ok ? QString() : errorText(r));
  });
}

void PersonalController::deleteCollection(const QString& collectionId) {
  begin();
  api_.deleteCollection(collectionId, [this, collectionId](const PersonalResult& r) {
    end();
    if (r.ok) {
      QList<CollectionInfo> all = model_->collections();
      all.removeIf([&](const CollectionInfo& c) { return c.id == collectionId; });
      model_->setCollections(all);  // the games stay; an open filter on it falls back to "all"
    }
    emit finished(QStringLiteral("delete"), r.ok, r.ok ? QString() : errorText(r));
  });
}

void PersonalController::setInCollection(const QString& collectionId, const QString& gameId, bool member) {
  model_->setCollectionMember(gameId, collectionId, member);
  begin();
  api_.setInCollection(collectionId, gameId, member, [this, collectionId, gameId, member](const PersonalResult& r) {
    end();
    if (!r.ok) {
      model_->setCollectionMember(gameId, collectionId, !member);
    }
    emit finished(QStringLiteral("member"), r.ok, r.ok ? QString() : errorText(r));
  });
}

}  // namespace uzume::ui
