#include "controllerscontroller.h"

#include <Qt>

#include "controllersglyphs.h"
#include "padtype.h"

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
    emit labelSetChanged();
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
    emit labelSetChanged();
  }
}

std::optional<ControllerProfile> ControllersController::currentProfile() const {
  const Selected s = selection();
  if (s.kind != QLatin1String("gamepad") && s.kind != kKeyboard) return std::nullopt;
  return profiles_.find(profiles_.assignedProfileId(s.deviceKey, s.kind));
}

void ControllersController::setSystemLabels(const QString& systemLabel, const QString& touchLabel) {
  if (systemLabel_ == systemLabel && touchLabel_ == touchLabel) return;
  systemLabel_ = systemLabel;
  touchLabel_ = touchLabel;
  emit labelsChanged();
  emit devicesChanged();
}

QVariantList ControllersController::devices() const {
  QVariantList l;
  for (const input::PadDevice& d : pads_.devices()) {
    const auto p = profiles_.find(profiles_.assignedProfileId(d.key, QStringLiteral("gamepad")));
    l.append(QVariantMap{{QStringLiteral("key"), kPadPrefix + QString::number(d.id)},
                         {QStringLiteral("kind"), QStringLiteral("gamepad")},
                         {QStringLiteral("name"), d.name},
                         {QStringLiteral("slot"), d.slot == 1 ? QStringLiteral("P1") : QStringLiteral("—")},
                         {QStringLiteral("connected"), true},
                         {QStringLiteral("status"), tr("Connected")},
                         {QStringLiteral("profile"), p ? p->name : QString()},
                         {QStringLiteral("padType"), input::padTypeId(d.type)},
                         {QStringLiteral("padTypeName"), input::padTypeName(d.type)}});
  }
  const auto kb = profiles_.find(profiles_.assignedProfileId(kKeyboard, kKeyboard));
  l.append(QVariantMap{{QStringLiteral("key"), kKeyboard},
                       {QStringLiteral("kind"), kKeyboard},
                       {QStringLiteral("name"), tr("Keyboard")},
                       {QStringLiteral("slot"), pads_.devices().isEmpty() ? QStringLiteral("P1") : QStringLiteral("—")},
                       {QStringLiteral("connected"), true},
                       {QStringLiteral("status"), tr("Connected")},
                       {QStringLiteral("profile"), kb ? kb->name : QString()}});
  l.append(QVariantMap{{QStringLiteral("key"), kMouse},
                       {QStringLiteral("kind"), kMouse},
                       {QStringLiteral("name"), tr("Mouse")},
                       {QStringLiteral("slot"), tr("Touch")},
                       {QStringLiteral("connected"), true},
                       {QStringLiteral("status"), tr("Connected")},
                       {QStringLiteral("profile"), touchLabel_}});
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
  const ControllerProfile def = ControllerProfiles::builtinProfile(p->kind);
  const QString set = labelSet();
  for (const InputDef& in : frameBeamInputs()) {
    const QStringList tokens = p->bindings.value(in.id);
    const bool changed = !p->builtin && tokens != def.bindings.value(in.id);
    QStringList labels;
    for (const QString& t : tokens) labels.append(tokenLabel(t));
    const QVariantMap d = describeBinding(set, tokens);
    l.append(QVariantMap{{QStringLiteral("input"), in.id},
                         {QStringLiteral("label"), in.label},
                         {QStringLiteral("target"), in.ndsTarget},
                         {QStringLiteral("binding"), labels.isEmpty() ? tr("Unassigned") : labels.join(QStringLiteral(" / "))},
                         {QStringLiteral("glyphs"), d.value(QStringLiteral("glyphs"))},
                         {QStringLiteral("bindingName"), labels.isEmpty() ? tr("Unassigned") : d.value(QStringLiteral("name"))},
                         {QStringLiteral("mapped"), !labels.isEmpty()},
                         {QStringLiteral("changed"), changed},
                         {QStringLiteral("listening"), listening_ == in.id}});
  }
  return l;
}

int ControllersController::changedCount() const {
  int n = 0;
  for (const QVariant& v : rows()) {
    n += v.toMap().value(QStringLiteral("changed")).toBool() ? 1 : 0;
  }
  return n;
}

void ControllersController::resetBinding(const QString& inputId) {
  const auto p = currentProfile();
  if (!p || p->builtin || inputIndex(inputId) < 0) return;
  cancelCapture();
  if (profiles_.setBinding(p->id, inputId, ControllerProfiles::builtinProfile(p->kind).bindings.value(inputId))) profileChanged();
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

QString ControllersController::labelSetFor(const Selected& s, const input::PadDevice& d) const {
  if (s.kind == kKeyboard) return kKeyboard;
  if (s.kind != QLatin1String("gamepad")) return QStringLiteral("generic");
  const QString override = profiles_.labelSet(s.deviceKey);
  return override.isEmpty() ? input::padTypeId(d.type) : override;
}

QString ControllersController::labelChoice() const {
  const Selected s = selection();
  if (s.kind != QLatin1String("gamepad")) return QStringLiteral("auto");
  const QString o = profiles_.labelSet(s.deviceKey);
  return o.isEmpty() ? QStringLiteral("auto") : o;
}

QString ControllersController::labelSet() const {
  const Selected s = selection();
  return labelSetFor(s, s.kind == QLatin1String("gamepad") ? pads_.device(s.padId) : input::PadDevice{});
}

QString ControllersController::labelSetName() const { return labelSetTitle(labelSet()); }

QVariantList ControllersController::labelChoices() const {
  const Selected s = selection();
  const input::PadDevice d = s.kind == QLatin1String("gamepad") ? pads_.device(s.padId) : input::PadDevice{};
  const auto entry = [](const QString& value, const QString& label) {
    return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("label"), label}};
  };
  return {entry(QStringLiteral("auto"), tr("Auto (%1)").arg(input::padTypeName(d.type))), entry(QStringLiteral("xbox"), tr("Xbox")),
          entry(QStringLiteral("playstation"), tr("PlayStation")), entry(QStringLiteral("generic"), tr("Generic"))};
}

void ControllersController::setLabelChoice(const QString& choice) {
  const Selected s = selection();
  if (s.kind != QLatin1String("gamepad")) return;
  if (choice != QLatin1String("auto") && choice != QLatin1String("xbox") && choice != QLatin1String("playstation") &&
      choice != QLatin1String("generic")) {
    return;
  }
  if (!profiles_.setLabelSet(s.deviceKey, choice == QLatin1String("auto") ? QString() : choice)) return;
  emit labelSetChanged();
  emit rowsChanged();
  emit testChanged();
}

QVariantList ControllersController::testCells() const {
  const Selected s = selection();
  QSet<QString> tokens;
  QStringList inputs;
  if (s.kind == QLatin1String("gamepad")) {
    tokens = pads_.pressedTokens(s.padId);
  } else {
    inputs = activeInputs();
  }
  QVariantList cells = inputTestCells(labelSet());
  for (QVariant& v : cells) {
    QVariantMap c = v.toMap();
    bool on = false;
    if (s.kind == QLatin1String("gamepad")) {
      for (const QString& t : c.value(QStringLiteral("tokens")).toStringList()) on = on || tokens.contains(t);
    } else {
      on = inputs.contains(c.value(QStringLiteral("id")).toString());
    }
    c.insert(QStringLiteral("active"), on);
    c.remove(QStringLiteral("tokens"));
    v = c;
  }
  return cells;
}

QHash<int, quint32> ControllersController::keyboardMap() const {
  const auto p = profiles_.find(profiles_.assignedProfileId(kKeyboard, kKeyboard));
  QHash<int, quint32> map = input::ndsKeyMap(p ? p->bindings : ControllerProfiles::builtinProfile(kKeyboard).bindings);
  for (auto it = map.begin(); it != map.end();) {  // a hotkey wins over the keyboard profile
    it = profiles_.hotkeyAction(it.key()).isEmpty() ? std::next(it) : map.erase(it);
  }
  return map;
}

namespace {
QString keyLabelOf(int key) { return key > 0 ? tokenLabel(keyToken(key)) : QString(); }
}  // namespace

QVariantList ControllersController::hotkeyRows() const {
  QVariantList l;
  const auto kb = profiles_.find(profiles_.assignedProfileId(kKeyboard, kKeyboard));
  for (const HotkeyDef& d : hotkeyDefs()) {
    const int key = profiles_.hotkey(d.id);
    QString conflict;
    if (key > 0 && kb) {
      const QString token = keyToken(key);
      for (const InputDef& in : frameBeamInputs()) {
        if (kb->bindings.value(in.id).contains(token)) {
          conflict = in.label;
          break;
        }
      }
    }
    l.append(QVariantMap{{QStringLiteral("action"), d.id},
                         {QStringLiteral("label"), d.label},
                         {QStringLiteral("key"), key > 0 ? keyLabelOf(key) : tr("not set")},
                         {QStringLiteral("set"), key > 0},
                         {QStringLiteral("changed"), key != d.defaultKey},
                         {QStringLiteral("listening"), hotkeyListening_ == d.id},
                         {QStringLiteral("fixed"), false},
                         {QStringLiteral("conflictInput"), conflict}});
  }
  l.append(QVariantMap{{QStringLiteral("action"), QStringLiteral("escape")},
                       {QStringLiteral("label"), tr("Leave fullscreen / pause")},
                       {QStringLiteral("key"), QStringLiteral("Esc")},
                       {QStringLiteral("set"), true},
                       {QStringLiteral("changed"), false},
                       {QStringLiteral("listening"), false},
                       {QStringLiteral("fixed"), true},
                       {QStringLiteral("conflictInput"), QString()}});
  return l;
}

QVariantMap ControllersController::hotkeyLabels() const {
  QVariantMap m;
  for (const HotkeyDef& d : hotkeyDefs()) m.insert(d.id, keyLabelOf(profiles_.hotkey(d.id)));
  return m;
}

QSet<int> ControllersController::hotkeyKeys() const {
  QSet<int> s;
  for (const HotkeyDef& d : hotkeyDefs()) {
    if (const int k = profiles_.hotkey(d.id); k > 0) s.insert(k);
  }
  return s;
}

void ControllersController::hotkeysUpdated() {
  emit hotkeysChanged();
  emit keyboardMapChanged();
}

void ControllersController::beginHotkeyCapture(const QString& action) {
  if (hotkeyIndex(action) < 0) return;
  cancelCapture();
  hotkeyListening_ = action;
  hotkeyNote_.clear();
  emit hotkeysChanged();
}

void ControllersController::cancelHotkeyCapture() {
  if (!hotkeyListening_.isEmpty() || !hotkeyNote_.isEmpty()) {
    hotkeyListening_.clear();
    hotkeyNote_.clear();
    emit hotkeysChanged();
  }
}

void ControllersController::clearHotkey(const QString& action) {
  cancelHotkeyCapture();
  if (profiles_.clearHotkey(action)) hotkeysUpdated();
}

void ControllersController::resetHotkey(const QString& action) {
  cancelHotkeyCapture();
  if (profiles_.resetHotkey(action)) hotkeysUpdated();
}

void ControllersController::resetHotkeys() {
  cancelHotkeyCapture();
  if (profiles_.resetHotkeys()) hotkeysUpdated();
}

bool ControllersController::captureHotkey(int qtKey) {
  if (qtKey == Qt::Key_Escape) {
    cancelHotkeyCapture();
    return true;
  }
  if (qtKey == 0 || qtKey == Qt::Key_unknown || qtKey == Qt::Key_Shift || qtKey == Qt::Key_Control || qtKey == Qt::Key_Alt ||
      qtKey == Qt::Key_Meta) {
    return true;
  }
  const QString action = hotkeyListening_;
  if (!profiles_.setHotkey(action, qtKey)) {
    const QString owner = profiles_.hotkeyAction(qtKey);
    for (const HotkeyDef& d : hotkeyDefs()) {
      if (d.id == owner) hotkeyNote_ = tr("Already used by %1").arg(d.label);
    }
    emit hotkeysChanged();  // the binding stays, the field keeps listening
    return true;
  }
  hotkeyListening_.clear();
  hotkeyNote_.clear();
  hotkeysUpdated();
  return true;
}

void ControllersController::duplicateAndCapture(const QString& inputId) {
  if (!profileBuiltin()) {
    beginCapture(inputId);
    return;
  }
  duplicateProfile();
  if (!profileBuiltin()) beginCapture(inputId);
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
  emit labelSetChanged();
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
  emit hotkeysChanged();  // conflictInput depends on the keyboard profile
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
  cancelHotkeyCapture();
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
  if (!hotkeyListening_.isEmpty()) return captureHotkey(qtKey);
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
