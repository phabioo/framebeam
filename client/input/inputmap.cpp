#include "inputmap.h"

#include "controllerprofiles.h"

#include <mutex>
#include <shared_mutex>

namespace uzume::input {

quint32 inputBit(const QString& inputId) {
  const int i = inputIndex(inputId);
  return i < 0 ? 0u : (1u << i);
}

namespace {
struct ProfileTable {
  QMap<QString, QString> map;     // as registered (conflict check)
  QMap<QString, QString> labels;
  QHash<QString, quint32> bits;   // input id -> libretro bits
};

std::shared_mutex& tablesMutex() {
  static std::shared_mutex m;
  return m;
}
QHash<QString, ProfileTable>& tables() {
  static QHash<QString, ProfileTable> t;
  return t;
}
// Caller holds the lock.
const ProfileTable* tableFor(const QString& profile) {
  const auto it = tables().constFind(profile);
  if (it != tables().cend()) return &it.value();
  const auto nds = tables().constFind(QStringLiteral("nds"));
  return nds != tables().cend() ? &nds.value() : nullptr;
}
}  // namespace

quint32 libretroTargetBit(const QString& target) {
  // RETRO_DEVICE_ID_JOYPAD_*: B=0 Y=1 SELECT=2 START=3 UP=4 DOWN=5 LEFT=6 RIGHT=7 A=8 X=9 L=10 R=11 L2=12 R2=13 L3=14 R3=15
  static const QHash<QString, quint32> ids = [] {
    QHash<QString, quint32> h;
    const char* joypad[] = {"b", "y", "select", "start", "up", "down", "left", "right", "a", "x", "l", "r", "l2", "r2", "l3", "r3"};
    for (int i = 0; i < 16; ++i) h.insert(QLatin1String(joypad[i]), 1u << i);
    const char* dirs[] = {"_up", "_down", "_left", "_right"};
    for (int i = 0; i < 4; ++i) {
      h.insert(QStringLiteral("c_stick") + QLatin1String(dirs[i]), 1u << (16 + i));
      h.insert(QStringLiteral("circle_pad") + QLatin1String(dirs[i]), 1u << (20 + i));
    }
    return h;
  }();
  return ids.value(target, 0u);
}

bool registerInputProfile(const QString& profile, const QMap<QString, QString>& map, const QMap<QString, QString>& labels,
                          QString* error) {
  auto fail = [&](const QString& m) {
    if (error) *error = m;
    return false;
  };
  if (profile.isEmpty()) return fail(QStringLiteral("input profile: empty name"));
  ProfileTable t;
  t.map = map;
  t.labels = labels;
  for (auto it = map.cbegin(); it != map.cend(); ++it) {
    if (inputIndex(it.key()) < 0) return fail(QStringLiteral("input.map: unknown input ") + it.key());
    const quint32 bit = libretroTargetBit(it.value());
    if (bit == 0) return fail(QStringLiteral("input.map: unknown target ") + it.value());
    t.bits.insert(it.key(), bit);
  }
  std::unique_lock lock(tablesMutex());
  const auto old = tables().constFind(profile);
  if (old != tables().cend()) {
    if (old->map == t.map && old->labels == t.labels) return true;
    return fail(QStringLiteral("input profile ") + profile + QStringLiteral(": conflicting maps"));
  }
  tables().insert(profile, t);
  return true;
}

void clearInputProfiles() {
  std::unique_lock lock(tablesMutex());
  tables().clear();
}

bool inputProfileMaps(const QString& profile, const QString& inputId) {
  std::shared_lock lock(tablesMutex());
  const ProfileTable* t = tableFor(profile);
  return t && t->bits.contains(inputId);
}

quint32 inputProfileBits(const QString& profile, const QString& inputId) {
  std::shared_lock lock(tablesMutex());
  const ProfileTable* t = tableFor(profile);
  return t ? t->bits.value(inputId, 0u) : 0u;
}

QString inputProfileLabel(const QString& profile, const QString& inputId) {
  std::shared_lock lock(tablesMutex());
  const ProfileTable* t = tableFor(profile);
  return t ? t->labels.value(inputId) : QString();
}

quint32 libretroMaskFor(const QString& inputProfile, quint32 inputMask) {
  std::shared_lock lock(tablesMutex());
  const ProfileTable* t = tableFor(inputProfile);
  if (!t) return 0;
  quint32 out = 0;
  const auto& inputs = uzumeInputs();
  for (int i = 0; i < inputs.size(); ++i) {
    if ((inputMask & (1u << i)) != 0) out |= t->bits.value(inputs.at(i).id, 0u);
  }
  return out;
}

QHash<QString, quint32> compileBindings(const Bindings& bindings) {
  QHash<QString, quint32> out;
  for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
    const quint32 bit = inputBit(it.key());
    if (bit == 0) continue;
    for (const QString& t : it.value()) out[t] |= bit;
  }
  return out;
}

QHash<int, quint32> keyMapFor(const QString& inputProfile, const Bindings& bindings) {
  QHash<int, quint32> out;
  const QHash<QString, quint32> c = compileBindings(bindings);
  for (auto it = c.cbegin(); it != c.cend(); ++it) {
    if (const auto k = keyFromToken(it.key())) out[*k] |= libretroMaskFor(inputProfile, it.value());
  }
  return out;
}

}  // namespace uzume::input
