#pragma once
// KeyedRowsModel: a list model of QVariantMap rows with a fixed set of role names, replaced as a whole. Rows are
// keyed by one role (e.g. "runId"): when the keys are unchanged only the changed rows are reported (no delegate churn),
// otherwise the model resets. QML lists (Repeater, ListView) use it instead of JS arrays, which Qt 6.4 handles badly
// in nested layouts.
#include <QAbstractListModel>
#include <QList>
#include <QStringList>
#include <QVariantMap>

namespace framebeam::ui {

class KeyedRowsModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
 public:
  KeyedRowsModel(const QStringList& roles, const QString& keyRole, QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override { return names_; }

  int count() const { return static_cast<int>(rows_.size()); }
  const QList<QVariantMap>& rows() const { return rows_; }
  void setRows(const QList<QVariantMap>& rows);
  Q_INVOKABLE QVariantMap get(int row) const { return row >= 0 && row < rows_.size() ? rows_.at(row) : QVariantMap(); }

 signals:
  void countChanged();

 private:
  QHash<int, QByteArray> names_;
  QStringList roles_;
  QString keyRole_;
  QList<QVariantMap> rows_;
};

}  // namespace framebeam::ui
