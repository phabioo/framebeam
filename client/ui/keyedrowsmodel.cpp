#include "keyedrowsmodel.h"

namespace framebeam::ui {

KeyedRowsModel::KeyedRowsModel(const QStringList& roles, const QString& keyRole, QObject* parent)
    : QAbstractListModel(parent), roles_(roles), keyRole_(keyRole) {
  for (int i = 0; i < roles.size(); ++i) names_.insert(Qt::UserRole + 1 + i, roles.at(i).toUtf8());
}

QVariant KeyedRowsModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
  const int i = role - Qt::UserRole - 1;
  if (i < 0 || i >= roles_.size()) return {};
  return rows_.at(index.row()).value(roles_.at(i));
}

void KeyedRowsModel::setRows(const QList<QVariantMap>& rows) {
  bool sameKeys = rows.size() == rows_.size();
  for (int i = 0; sameKeys && i < rows.size(); ++i) sameKeys = rows.at(i).value(keyRole_) == rows_.at(i).value(keyRole_);
  const int before = count();
  if (sameKeys) {
    for (int i = 0; i < rows.size(); ++i) {
      if (rows.at(i) == rows_.at(i)) continue;
      rows_[i] = rows.at(i);
      emit dataChanged(index(i), index(i));
    }
    return;
  }
  beginResetModel();
  rows_ = rows;
  endResetModel();
  if (before != count()) emit countChanged();
}

}  // namespace framebeam::ui
