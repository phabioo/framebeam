#include "hoststate.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace framebeam {

namespace {
constexpr int kMaxNameChars = 64;

QString cleanName(const QString& raw) {
  QString out;
  out.reserve(raw.size());
  for (const QChar c : raw) {
    if (!c.isNonCharacter() && c.category() != QChar::Other_Control && c.category() != QChar::Other_Format) out.append(c);
  }
  return out.trimmed().left(kMaxNameChars);
}
}  // namespace

QByteArray makeHostState(bool paused, const QString& by) {
  QJsonObject o{{QStringLiteral("t"), QStringLiteral("state")}, {QStringLiteral("paused"), paused}};
  const QString name = cleanName(by);
  if (paused && !name.isEmpty()) o.insert(QStringLiteral("by"), name);
  return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<HostState> parseHostState(const QByteArray& message) {
  if (message.isEmpty() || message.size() > 512) return std::nullopt;
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(message, &pe);
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
  const QJsonObject o = doc.object();
  if (o.value(QStringLiteral("t")).toString() != QLatin1String("state")) return std::nullopt;
  const QJsonValue paused = o.value(QStringLiteral("paused"));
  if (!paused.isBool()) return std::nullopt;
  HostState s;
  s.paused = paused.toBool();
  const QJsonValue by = o.value(QStringLiteral("by"));
  if (s.paused && by.isString()) s.by = cleanName(by.toString());
  return s;
}

}  // namespace framebeam
