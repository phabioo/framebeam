#include "inputmap.h"

#include "controllerprofiles.h"

namespace framebeam::input {

quint32 inputBit(const QString& inputId) {
  const int i = inputIndex(inputId);
  return i < 0 ? 0u : (1u << i);
}

namespace {
// RETRO_DEVICE_ID_JOYPAD_*: B=0 Y=1 SELECT=2 START=3 UP=4 DOWN=5 LEFT=6 RIGHT=7 A=8 X=9 L=10 R=11
unsigned ndsRetroId(const QString& inputId) {
  static const QHash<QString, unsigned> ids = {
      {QStringLiteral("b"), 0},     {QStringLiteral("y"), 1},    {QStringLiteral("select"), 2}, {QStringLiteral("start"), 3},
      {QStringLiteral("up"), 4},    {QStringLiteral("down"), 5}, {QStringLiteral("left"), 6},   {QStringLiteral("right"), 7},
      {QStringLiteral("a"), 8},     {QStringLiteral("x"), 9},    {QStringLiteral("l"), 10},     {QStringLiteral("r"), 11},
  };
  return ids.value(inputId, 31u);
}
}  // namespace

quint32 ndsLibretroMask(quint32 inputMask) {
  quint32 out = 0;
  const auto& inputs = frameBeamInputs();
  for (int i = 0; i < inputs.size(); ++i) {
    if ((inputMask & (1u << i)) != 0) {
      const unsigned id = ndsRetroId(inputs.at(i).id);
      if (id < 31u) out |= 1u << id;
    }
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

QHash<int, quint32> ndsKeyMap(const Bindings& bindings) {
  QHash<int, quint32> out;
  const QHash<QString, quint32> c = compileBindings(bindings);
  for (auto it = c.cbegin(); it != c.cend(); ++it) {
    if (const auto k = keyFromToken(it.key())) out[*k] |= ndsLibretroMask(it.value());
  }
  return out;
}

}  // namespace framebeam::input
