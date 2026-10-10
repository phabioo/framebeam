#include "inputmap.h"

#include "controllerprofiles.h"

namespace framebeam::input {

quint32 inputBit(const QString& inputId) {
  const int i = inputIndex(inputId);
  return i < 0 ? 0u : (1u << i);
}

namespace {
// RETRO_DEVICE_ID_JOYPAD_*: B=0 Y=1 SELECT=2 START=3 UP=4 DOWN=5 LEFT=6 RIGHT=7 A=8 X=9 L=10 R=11 L2=12 R2=14
const QHash<QString, quint32>& baseBits() {
  static const QHash<QString, quint32> ids = {
      {QStringLiteral("b"), 1u << 0},      {QStringLiteral("y"), 1u << 1},     {QStringLiteral("select"), 1u << 2},
      {QStringLiteral("start"), 1u << 3},  {QStringLiteral("up"), 1u << 4},    {QStringLiteral("down"), 1u << 5},
      {QStringLiteral("left"), 1u << 6},   {QStringLiteral("right"), 1u << 7}, {QStringLiteral("a"), 1u << 8},
      {QStringLiteral("x"), 1u << 9},      {QStringLiteral("l"), 1u << 10},    {QStringLiteral("r"), 1u << 11},
  };
  return ids;
}
// nds: the DS has no circle pad, so it acts as a second D-pad (the left stick of the built-in gamepad profile).
const QHash<QString, quint32>& ndsBits() {
  static const QHash<QString, quint32> ids = [] {
    QHash<QString, quint32> h = baseBits();
    h.insert(QStringLiteral("zl"), 1u << 10);  // ... and ZL/ZR into L/R (the built-in gamepad binds the triggers to them)
    h.insert(QStringLiteral("zr"), 1u << 11);
    h.insert(QStringLiteral("lup"), 1u << 4);
    h.insert(QStringLiteral("ldown"), 1u << 5);
    h.insert(QStringLiteral("lleft"), 1u << 6);
    h.insert(QStringLiteral("lright"), 1u << 7);
    return h;
  }();
  return ids;
}
// 3ds: separate D-pad and circle pad (left analog), ZL/ZR = L2/R2, C-stick = right analog.
const QHash<QString, quint32>& threeDsBits() {
  static const QHash<QString, quint32> ids = [] {
    QHash<QString, quint32> h = baseBits();
    h.insert(QStringLiteral("zl"), 1u << 12);
    h.insert(QStringLiteral("zr"), 1u << 14);
    h.insert(QStringLiteral("cup"), kLibretroCStickUp);
    h.insert(QStringLiteral("cdown"), kLibretroCStickDown);
    h.insert(QStringLiteral("cleft"), kLibretroCStickLeft);
    h.insert(QStringLiteral("cright"), kLibretroCStickRight);
    h.insert(QStringLiteral("lup"), kLibretroCirclePadUp);
    h.insert(QStringLiteral("ldown"), kLibretroCirclePadDown);
    h.insert(QStringLiteral("lleft"), kLibretroCirclePadLeft);
    h.insert(QStringLiteral("lright"), kLibretroCirclePadRight);
    return h;
  }();
  return ids;
}

quint32 mapMask(const QHash<QString, quint32>& table, quint32 inputMask) {
  quint32 out = 0;
  const auto& inputs = frameBeamInputs();
  for (int i = 0; i < inputs.size(); ++i) {
    if ((inputMask & (1u << i)) != 0) out |= table.value(inputs.at(i).id, 0u);
  }
  return out;
}
}  // namespace

quint32 ndsLibretroMask(quint32 inputMask) { return mapMask(ndsBits(), inputMask); }
quint32 threeDsLibretroMask(quint32 inputMask) { return mapMask(threeDsBits(), inputMask); }

quint32 libretroMaskFor(const QString& inputProfile, quint32 inputMask) {
  return inputProfile == QLatin1String("3ds") ? threeDsLibretroMask(inputMask) : ndsLibretroMask(inputMask);
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

QHash<int, quint32> ndsKeyMap(const Bindings& bindings) { return keyMapFor(QStringLiteral("nds"), bindings); }

QHash<int, quint32> keyMapFor(const QString& inputProfile, const Bindings& bindings) {
  QHash<int, quint32> out;
  const QHash<QString, quint32> c = compileBindings(bindings);
  for (auto it = c.cbegin(); it != c.cend(); ++it) {
    if (const auto k = keyFromToken(it.key())) out[*k] |= libretroMaskFor(inputProfile, it.value());
  }
  return out;
}

}  // namespace framebeam::input
