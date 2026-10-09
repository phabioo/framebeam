#include "corechoice.h"

namespace framebeam {

CoreChoice chooseCore(const SystemInfo& system, const EmulationSettings& settings, const QString& gameId,
                      const std::function<QString(const QString&)>& canonical) {
  using L = EmulationSettings::Level;
  const auto canon = [&](const QString& id) { return canonical ? canonical(id) : id; };
  // The cores the Hub serves; a Hub without cores_v2 serves exactly its (legacy) default core.
  QList<SystemCore> served = system.cores;
  const SystemCore def = system.defaultCore();
  if (served.isEmpty() && def.valid()) {
    served.append(def);
  }
  const auto find = [&](const QString& id) -> const SystemCore* {
    for (const SystemCore& c : std::as_const(served)) {
      if (c.coreId == id) return &c;
    }
    for (const SystemCore& c : std::as_const(served)) {
      if (canon(c.coreId) == canon(id)) return &c;
    }
    return nullptr;
  };
  CoreChoice out;
  struct Level {
    L level;
    QString scope;
    const char* name;
  };
  for (const Level& l : {Level{L::Game, gameId, "game"}, Level{L::System, system.id, "system"}}) {
    if (l.scope.isEmpty() || !settings.hasValue(l.level, l.scope, QString::fromLatin1(EmulationSettings::kCoreKey))) continue;
    const QString stored = settings.value(l.level, l.scope, QString::fromLatin1(EmulationSettings::kCoreKey));
    if (const SystemCore* c = find(stored)) {
      out.core = *c;
      out.source = QString::fromLatin1(l.name);
      return out;
    }
    if (out.staleChoice.isEmpty()) {
      out.staleChoice = stored;
      out.staleLevel = QString::fromLatin1(l.name);
    }
  }
  if (def.valid()) {
    out.core = def;
    out.source = QStringLiteral("hub");
  } else {
    out.source = QStringLiteral("none");
  }
  return out;
}

}  // namespace framebeam
