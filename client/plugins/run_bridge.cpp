#include "run_bridge.h"

#include <cmath>
#include <map>

namespace framebeam::plugins {

Value valueFromJson(const QJsonValue& j, int depth) {
  Value v;
  if (depth > kMaxValueDepth) return v;
  switch (j.type()) {
    case QJsonValue::Bool:
      v.type = Value::Type::Bool;
      v.b = j.toBool();
      break;
    case QJsonValue::Double: {
      const double d = j.toDouble();
      if (std::floor(d) == d && std::fabs(d) < 9.0e15) {
        v.type = Value::Type::Int;
        v.i = static_cast<int64_t>(d);
      } else {
        v.type = Value::Type::Float;
        v.d = d;
      }
      break;
    }
    case QJsonValue::String:
      v.type = Value::Type::String;
      v.s = j.toString().toStdString();
      break;
    case QJsonValue::Array: {
      v.type = Value::Type::Table;
      const QJsonArray a = j.toArray();
      for (int i = 0; i < a.size(); ++i) {
        Value k;
        k.type = Value::Type::Int;
        k.i = i + 1;
        v.keys.push_back(k);
        v.vals.push_back(valueFromJson(a.at(i), depth + 1));
      }
      break;
    }
    case QJsonValue::Object: {
      v.type = Value::Type::Table;
      const QJsonObject o = j.toObject();
      for (auto it = o.begin(); it != o.end(); ++it) {
        Value k;
        k.type = Value::Type::String;
        k.s = it.key().toStdString();
        v.keys.push_back(k);
        v.vals.push_back(valueFromJson(it.value(), depth + 1));
      }
      break;
    }
    default:
      break;  // null / undefined -> nil
  }
  return v;
}

bool jsonFromValue(const Value& v, QJsonValue& out, QString& err, int depth) {
  if (depth > kMaxValueDepth) {
    err = QStringLiteral("nested too deeply");
    return false;
  }
  switch (v.type) {
    case Value::Type::Nil:
      out = QJsonValue(QJsonValue::Null);
      return true;
    case Value::Type::Bool:
      out = v.b;
      return true;
    case Value::Type::Int:
      out = static_cast<double>(v.i);
      return true;
    case Value::Type::Float:
      if (!std::isfinite(v.d)) {
        err = QStringLiteral("numbers must be finite");
        return false;
      }
      out = v.d;
      return true;
    case Value::Type::String:
      out = QString::fromStdString(v.s);
      return true;
    case Value::Type::Table:
      break;
  }
  // Array when the keys are exactly 1..n.
  bool isArray = true;
  std::map<int64_t, size_t> byIndex;
  for (size_t i = 0; i < v.keys.size(); ++i) {
    if (v.keys[i].type != Value::Type::Int || v.keys[i].i < 1) {
      isArray = false;
      break;
    }
    byIndex[v.keys[i].i] = i;
  }
  if (isArray && byIndex.size() != v.keys.size()) isArray = false;
  if (isArray) {
    int64_t expect = 1;
    for (const auto& kv : byIndex)
      if (kv.first != expect++) {
        isArray = false;
        break;
      }
  }
  if (isArray) {
    QJsonArray a;
    for (const auto& kv : byIndex) {
      QJsonValue e;
      if (!jsonFromValue(v.vals[kv.second], e, err, depth + 1)) return false;
      a.append(e);
    }
    out = a;
    return true;
  }
  QJsonObject o;
  for (size_t i = 0; i < v.keys.size(); ++i) {
    if (v.keys[i].type != Value::Type::String) {
      err = QStringLiteral("table keys must be strings (or the table a list 1..n)");
      return false;
    }
    QJsonValue e;
    if (!jsonFromValue(v.vals[i], e, err, depth + 1)) return false;
    o.insert(QString::fromStdString(v.keys[i].s), e);
  }
  out = o;
  return true;
}

bool jsonObjectFromValue(const Value& v, QJsonObject& out, QString& err) {
  if (v.type != Value::Type::Table) {
    err = QStringLiteral("expected a table");
    return false;
  }
  if (v.keys.empty()) {
    out = QJsonObject();
    return true;
  }
  QJsonValue j;
  if (!jsonFromValue(v, j, err)) return false;
  if (!j.isObject()) {
    err = QStringLiteral("expected a table with string keys, not a list");
    return false;
  }
  out = j.toObject();
  return true;
}

}  // namespace framebeam::plugins
