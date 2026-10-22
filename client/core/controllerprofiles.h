#pragma once
// Local controller profiles (<data>/settings/controllers.json): physical binding -> Uzume input.
// Profiles and device assignments stay local on this device and are never synchronized (architecture 06).
// Built-in profiles ("Standard Gamepad", "Keyboard · Standard") are read-only and not stored; user profiles
// are stored completely. Unknown keys of the file are preserved when saving. Never contains a secret.
//
// Binding tokens (strings, stable in the file):
//   gamepad:  "pad:<SDL gamepad button name>" (a b x y back guide start leftstick rightstick leftshoulder
//             rightshoulder dpup dpdown dpleft dpright), "pad:lefttrigger" / "pad:righttrigger" (trigger as button),
//             "pad:leftx-" "pad:leftx+" "pad:lefty-" "pad:lefty+" (left stick direction as a button; y- = up),
//             "pad:rightx-" "pad:rightx+" "pad:righty-" "pad:righty+" (right stick direction, same convention)
//   keyboard: "key:<Qt::Key as a number>"
// Names follow the position of the buttons (SDL): "pad:b" is the east button, i.e. "B" on an Xbox controller.

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace uzume {

// Uzume inputs (uniform across devices); the input profile of the system manifest maps them to the libretro mask.
struct InputDef {
  QString id;         // "a", "b", ..., "up"
  QString label;      // shown in the mapping table
  QString targetLabel;  // default text of the "System button" column; the manifest "input"."labels" can override it per profile
};
const QList<InputDef>& uzumeInputs();
int inputIndex(const QString& id);  // position in uzumeInputs(), -1 = unknown; bit index of input masks

struct ControllerProfile {
  QString id;
  QString name;
  QString kind;  // "gamepad" | "keyboard"
  bool builtin = false;
  QMap<QString, QStringList> bindings;  // input id -> tokens; missing/empty = not mapped
};

QString padToken(const QString& sdlName);
bool isPadToken(const QString& token);
QString keyToken(int qtKey);
std::optional<int> keyFromToken(const QString& token);
QString tokenLabel(const QString& token);  // "B", "LB", "Left stick ◀", "Return", ...

// Player hotkeys (global, never forwarded to the core; stored in controllers.json under "hotkeys" as
// {actionId: "key:<Qt::Key>" | "none"}, only non-default entries). One entry per action: a later action is one more line
// in hotkeyDefs(). Esc is fixed (leave fullscreen, else pause), not an action and never assignable.
// Gamepad hotkeys (P1) are stored separately under "padHotkeys" ({actionId: binding | "none"}, only non-default) and
// "padHotkeyModifier" (token, default pad:back). A binding is "pad:<button>" (modifier held + button) or
// "solo:pad:<button>" (the button alone); "" = unassigned.
struct HotkeyDef {
  QString id;       // "fullscreen"
  QString label;    // shown in the Hotkeys table
  int defaultKey;   // Qt::Key
  QString defaultPad = {};  // default gamepad binding, "" = none
};
QString padHotkeyToken(const QString& binding);  // "solo:pad:x" / "pad:x" -> "pad:x"
bool padHotkeySolo(const QString& binding);
QString padHotkeyBinding(const QString& token, bool solo);
// Short label for hints: "Select+RB", "R3", ... (the modifier token is only used for non-solo bindings).
QString padHotkeyLabel(const QString& binding, const QString& modifierToken);
const QList<HotkeyDef>& hotkeyDefs();
int hotkeyIndex(const QString& id);  // -1 = unknown

class ControllerProfiles {
 public:
  static constexpr const char* kBuiltinGamepadId = "builtin-gamepad";
  static constexpr const char* kBuiltinKeyboardId = "builtin-keyboard";
  static constexpr const char* kKeyboardDevice = "keyboard";

  // baseDir = data directory. A missing or corrupted file yields only the built-in profiles.
  explicit ControllerProfiles(const QString& baseDir);

  static ControllerProfile builtinProfile(const QString& kind);  // default bindings of a kind

  QString filePath() const { return path_; }
  QList<ControllerProfile> all() const;                       // built-in first, then user profiles in creation order
  QList<ControllerProfile> ofKind(const QString& kind) const;
  std::optional<ControllerProfile> find(const QString& id) const;

  // Returns the id of the new user profile (empty on error). Name defaults to "<name> copy".
  QString duplicate(const QString& id, const QString& newName = QString());
  bool rename(const QString& id, const QString& name);  // user profiles only, non-empty name
  bool remove(const QString& id);                       // user profiles only; devices fall back to the built-in one
  // Binding of one input (empty list = not mapped); user profiles only.
  bool setBinding(const QString& id, const QString& inputId, const QStringList& tokens);
  bool resetToDefault(const QString& id);  // user profiles only: bindings of the built-in profile of its kind

  // Hotkeys. key = Qt::Key, 0 = unassigned. setHotkey rejects a key used by another action or Esc (and unknown actions).
  int hotkey(const QString& actionId) const;
  bool setHotkey(const QString& actionId, int qtKey);
  bool clearHotkey(const QString& actionId);  // unassigned
  bool resetHotkey(const QString& actionId);
  bool resetHotkeys();
  QString hotkeyAction(int qtKey) const;      // action id the key triggers, empty = none (Esc is not an action)
  QMap<QString, int> hotkeys() const { return hotkeys_; }  // effective key per action id (0 = unassigned)
  int hotkeysChangedCount() const;  // keyboard + gamepad hotkeys and the modifier that differ from the default

  // Gamepad hotkeys (P1). setPadHotkey rejects a token used by another action, the modifier, "guide", non-pad tokens.
  QString padHotkey(const QString& actionId) const;  // effective binding, "" = unassigned
  bool setPadHotkey(const QString& actionId, const QString& binding);
  bool clearPadHotkey(const QString& actionId);
  bool resetPadHotkey(const QString& actionId);
  QString padHotkeyAction(const QString& token) const;  // action bound to this button (any mode), empty = none
  QMap<QString, QString> padHotkeys() const { return padHotkeys_; }
  QString padHotkeyModifier() const { return padModifier_; }
  bool setPadHotkeyModifier(const QString& token);  // rejects a token used by an action
  bool resetPadHotkeyModifier();
  bool padHotkeyModifierChanged() const;

  // Device assignment: deviceKey = gamepad GUID string or kKeyboardDevice. Falls back to the built-in profile of
  // the kind when nothing (valid) is assigned.
  QString assignedProfileId(const QString& deviceKey, const QString& kind) const;
  bool assign(const QString& deviceKey, const QString& profileId);

  // "Button labels" override per physical device (not per profile; stored under "labelSets"): "" = Auto (detected
  // controller type), else "xbox" | "playstation" | "generic". Unknown values read as Auto and are not stored.
  QString labelSet(const QString& deviceKey) const { return labelSets_.value(deviceKey); }
  bool setLabelSet(const QString& deviceKey, const QString& value);

 private:
  bool save() const;
  QString uniqueName(const QString& base) const;

  QString path_;
  QJsonObject raw_;
  QList<ControllerProfile> user_;
  QMap<QString, QString> assignments_;
  QMap<QString, QString> labelSets_;  // device key -> override (Auto = absent)
  QMap<QString, int> hotkeys_;  // effective key per action id, all actions present
  QMap<QString, QString> padHotkeys_;  // effective gamepad binding per action id, all actions present
  QString padModifier_;
};

}  // namespace uzume
