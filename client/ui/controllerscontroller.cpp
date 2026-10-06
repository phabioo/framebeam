#include "controllerscontroller.h"

#include <Qt>

namespace framebeam::ui {

namespace {
const QString kKeyboard = QStringLiteral("keyboard");
const QString kMouse = QStringLiteral("mouse");
const QString kPadPrefix = QStringLiteral("pad:");
}  // namespace

ControllersController::ControllersController(const QString& dataDir, QObject* parent)
    : QObject(parent), profiles_(dataDir) {
  pads_.setBindingsProvider([this](const QString& deviceKey) {
    const QString id = profiles_.assignedProfileId(deviceKey, QStringLiteral("gamepad"));
    const auto p = profiles_.find(id);
    return p ? p->bindings : ControllerProfiles::builtinProfile(QStringLiteral("gamepad")).bindings;
  });
  connect(&pads_, &input::GamepadService::devicesChanged, this, [this]() {
    ensureSelection();
    emit devicesChanged();
    emit profilesChanged();
    emit rowsChanged();
    emit testChanged();
  });
  connect(&pads_, &input::GamepadService::stateChanged, this, &ControllersController::testChanged);
  connect(&pads_, &input::GamepadService::libretroMaskChanged, this, &ControllersController::libretroMaskChanged);
  connect(&pads_, &input::GamepadService::captured, this, [this](const QString& token, int) { applyCapture(token); });
}

void ControllersController::start(bool enableGamepads, int pollIntervalMs) {
  if (!enableGamepads) {
    note_ = tr("Gamepads are turned off.");
    return;
  }
  if (!pads_.start(pollIntervalMs)) {
    note_ = tr("Gamepads are not available (%1). Keyboard and mouse still work.").arg(pads_.error());
  }
  ensureSelection();
}

ControllersController::Selected ControllersController::selection() const {
  Selected s;
  if (selected_ == kKeyboard) {
    s.kind = kKeyboard;
    s.deviceKey = kKeyboard;
  } else if (selected_ == kMouse) {
    s.kind = kMouse;
  } else if (selected_.startsWith(kPadPrefix)) {
    const input::PadDevice d = pads_.device(selected_.mid(kPadPrefix.size()).toInt());
    if (d.id != 0) {
      s.kind = QStringLiteral("gamepad");
      s.deviceKey = d.key;
      s.padId = d.id;
    }
  }
  return s;
}

void ControllersController::ensureSelection() {
  if (selection().kind.isEmpty()) {
    const auto pads = pads_.devices();
    selected_ = pads.isEmpty() ? kKeyboard : kPadPrefix + QString::number(pads.first().id);
    listening_.clear();
    pads_.cancelCapture();
    emit selectionChanged();
  }
}

std::optional<ControllerProfile> ControllersController::currentProfile() const {
  const Selected s = selection();
  if (s.kind != QLatin1String("gamepad") && s.kind != kKeyboard) return std::nullopt;
  return profiles_.find(profiles_.assignedProfileId(s.deviceKey, s.kind));
}

QVariantList ControllersController::devices() const {
  QVariantList l;
  for (const input::PadDevice& d : pads_.devices()) {
    const auto p = profiles_.find(profiles_.assignedProfileId(d.key, QStringLiteral("gamepad")));
    l.append(QVariantMap{{QStringLiteral("key"), kPadPrefix + QString::number(d.id)},
                         {QStringLiteral("kind"), QStringLiteral("gamepad")},
                         {QStringLiteral("name"), d.name},
                         {QStringLiteral("slot"), d.slot == 1 ? QStringLiteral("P1") : QStringLiteral("—")},
                         {QStringLiteral("profile"), p ? p->name : QString()}});
  }
  const auto kb = profiles_.find(profiles_.assignedProfileId(kKeyboard, kKeyboard));
  l.append(QVariantMap{{QStringLiteral("key"), kKeyboard},
                       {QStringLiteral("kind"), kKeyboard},
                       {QStringLiteral("name"), tr("Keyboard")},
                       {QStringLiteral("slot"), pads_.devices().isEmpty() ? QStringLiteral("P1") : QStringLiteral("—")},
                       {QStringLiteral("profile"), kb ? kb->name : QString()}});
  l.append(QVariantMap{{QStringLiteral("key"), kMouse},
                       {QStringLiteral("kind"), kMouse},
                       {QStringLiteral("name"), tr("Mouse")},
                       {QStringLiteral("slot"), tr("Touch")},
                       {QStringLiteral("profile"), tr("DS touch")}});
  return l;
}

QVariantMap ControllersController::device() const {
  for (const QVariant& v : devices()) {
    if (v.toMap().value(QStringLiteral("key")).toString() == selected_) return v.toMap();
  }
  return {};
}

QVariantList ControllersController::profiles() const {
  QVariantList l;
  const Selected s = selection();
  if (s.kind != QLatin1String("gamepad") && s.kind != kKeyboard) return l;
  for (const ControllerProfile& p : profiles_.ofKind(s.kind)) {
    l.append(QVariantMap{{QStringLiteral("value"), p.id}, {QStringLiteral("label"), p.name}, {QStringLiteral("builtin"), p.builtin}});
  }
  return l;
}

QString ControllersController::profileId() const {
  const auto p = currentProfile();
  return p ? p->id : QString();
}
QString ControllersController::profileName() const {
  const auto p = currentProfile();
  return p ? p->name : QString();
}
bool ControllersController::profileBuiltin() const {
  const auto p = currentProfile();
  return !p || p->builtin;
}

QVariantList ControllersController::rows() const {
  QVariantList l;
  const auto p = currentProfile();
  if (!p) return l;
  for (const InputDef& in : frameBeamInputs()) {
    const QStringList tokens = p->bindings.value(in.id);
    QStringList labels;
    for (const QString& t : tokens) labels.append(tokenLabel(t));
    l.append(QVariantMap{{QStringLiteral("input"), in.id},
                         {QStringLiteral("label"), in.label},
                         {QStringLiteral("target"), in.ndsTarget},
                         {QStringLiteral("binding"), labels.isEmpty() ? tr("not mapped") : labels.join(QStringLiteral(" / "))},
                         {QStringLiteral("mapped"), !labels.isEmpty()},
                         {QStringLiteral("listening"), listening_ == in.id}});
  }
  return l;
}

QStringList ControllersController::activeInputs() const {
  const Selected s = selection();
  quint32 mask = 0;
  if (s.kind == QLatin1String("gamepad")) {
    mask = pads_.inputMaskFor(s.padId);
  } else if (s.kind == kKeyboard) {
    if (const auto p = currentProfile()) {
      const QHash<QString, quint32> c = input::compileBindings(p->bindings);
      for (int k : std::as_const(heldKeys_)) mask |= c.value(keyToken(k), 0u);
    }
  }
  QStringList out;
  const auto& inputs = frameBeamInputs();
  for (int i = 0; i < inputs.size(); ++i) {
    if ((mask & (1u << i)) != 0) out.append(inputs.at(i).id);
  }
  return out;
}

QHash<int, quint32> ControllersController::keyboardMap() const {
  const auto p = profiles_.find(profiles_.assignedProfileId(kKeyboard, kKeyboard));
  return input::ndsKeyMap(p ? p->bindings : ControllerProfiles::builtinProfile(kKeyboard).bindings);
}

void ControllersController::selectDevice(const QString& key) {
  if (key == selected_) return;
  const QString before = selected_;
  selected_ = key;
  if (selection().kind.isEmpty()) {
    selected_ = before;
    return;
  }
  cancelCapture();
  heldKeys_.clear();
  emit selectionChanged();
  emit profilesChanged();
  emit rowsChanged();
  emit testChanged();
}

void ControllersController::profileChanged() {
  pads_.invalidateBindings();
  emit devicesChanged();
  emit profilesChanged();
  emit rowsChanged();
  emit testChanged();
  emit keyboardMapChanged();
}

void ControllersController::selectProfile(const QString& id) {
  const Selected s = selection();
  const auto p = profiles_.find(id);
  if (s.deviceKey.isEmpty() || !p || p->kind != s.kind) return;
  cancelCapture();
  profiles_.assign(s.deviceKey, id);
  profileChanged();
}

void ControllersController::duplicateProfile() {
  const Selected s = selection();
  const auto p = currentProfile();
  if (!p || s.deviceKey.isEmpty()) return;
  const QString id = profiles_.duplicate(p->id);
  if (!id.isEmpty()) {
    cancelCapture();
    profiles_.assign(s.deviceKey, id);
    profileChanged();
  }
}

void ControllersController::renameProfile(const QString& name) {
  const auto p = currentProfile();
  if (p && !p->builtin && profiles_.rename(p->id, name)) profileChanged();
}

void ControllersController::deleteProfile() {
  const auto p = currentProfile();
  if (p && !p->builtin) {
    cancelCapture();
    profiles_.remove(p->id);
    profileChanged();
  }
}

void ControllersController::resetProfile() {
  const auto p = currentProfile();
  if (p && !p->builtin) {
    cancelCapture();
    profiles_.resetToDefault(p->id);
    profileChanged();
  }
}

void ControllersController::beginCapture(const QString& inputId) {
  const auto p = currentProfile();
  const Selected s = selection();
  if (!p || p->builtin || inputIndex(inputId) < 0) return;  // built-in profiles are read-only
  listening_ = inputId;
  if (s.kind == QLatin1String("gamepad")) pads_.startCapture();
  emit rowsChanged();
}

void ControllersController::cancelCapture() {
  pads_.cancelCapture();
  if (!listening_.isEmpty()) {
    listening_.clear();
    emit rowsChanged();
  }
}

void ControllersController::clearBinding(const QString& inputId) {
  const auto p = currentProfile();
  if (!p || p->builtin) return;
  cancelCapture();
  if (profiles_.setBinding(p->id, inputId, {})) profileChanged();
}

void ControllersController::applyCapture(const QString& token) {
  const auto p = currentProfile();
  if (listening_.isEmpty() || !p || p->builtin) return;
  const QString input = listening_;
  listening_.clear();
  // A token drives one input: it is taken away from the input that had it before ("not mapped" there).
  for (auto it = p->bindings.cbegin(); it != p->bindings.cend(); ++it) {
    if (it.key() != input && it.value().contains(token)) {
      QStringList rest = it.value();
      rest.removeAll(token);
      profiles_.setBinding(p->id, it.key(), rest);
    }
  }
  profiles_.setBinding(p->id, input, {token});
  profileChanged();
}

bool ControllersController::captureKey(int qtKey) {
  const Selected s = selection();
  if (listening_.isEmpty() || s.kind != kKeyboard) return false;
  if (qtKey == Qt::Key_Escape) {
    cancelCapture();
    return true;
  }
  if (qtKey == 0 || qtKey == Qt::Key_unknown || qtKey == Qt::Key_Shift || qtKey == Qt::Key_Control || qtKey == Qt::Key_Alt ||
      qtKey == Qt::Key_Meta) {
    return true;  // modifiers alone are not bindable; swallow them while listening
  }
  applyCapture(keyToken(qtKey));
  return true;
}

void ControllersController::testKey(int qtKey, bool pressed) {
  if (selection().kind != kKeyboard) return;
  if (pressed ? !heldKeys_.contains(qtKey) : heldKeys_.contains(qtKey)) {
    if (pressed) heldKeys_.insert(qtKey); else heldKeys_.remove(qtKey);
    emit testChanged();
  }
}

void ControllersController::clearTestKeys() {
  if (!heldKeys_.isEmpty()) {
    heldKeys_.clear();
    emit testChanged();
  }
}

}  // namespace framebeam::ui
