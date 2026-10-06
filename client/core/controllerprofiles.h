#pragma once
// Local controller profiles (<data>/settings/controllers.json): physical binding -> FrameBeam input.
// Profiles and device assignments stay local on this device and are never synchronized (architecture 06).
// Built-in profiles ("Standard Gamepad", "Keyboard · Standard") are read-only and not stored; user profiles
// are stored completely. Unknown keys of the file are preserved when saving. Never contains a secret.
//
// Binding tokens (strings, stable in the file):
//   gamepad:  "pad:<SDL gamepad button name>" (a b x y back guide start leftstick rightstick leftshoulder
//             rightshoulder dpup dpdown dpleft dpright), "pad:lefttrigger" / "pad:righttrigger" (trigger as button),
//             "pad:leftx-" "pad:leftx+" "pad:lefty-" "pad:lefty+" (left stick direction as a button; y- = up)
//   keyboard: "key:<Qt::Key as a number>"
// Names follow the position of the buttons (SDL): "pad:b" is the east button, i.e. "B" on an Xbox controller.

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace framebeam {

// FrameBeam inputs (uniform across devices); the system profile (nds) maps them to the libretro joypad.
struct InputDef {
  QString id;         // "a", "b", ..., "up"
  QString label;      // shown in the mapping table
  QString ndsTarget;  // target in the nds system profile (shown in the table)
};
const QList<InputDef>& frameBeamInputs();
int inputIndex(const QString& id);  // position in frameBeamInputs(), -1 = unknown; bit index of input masks

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

  // Device assignment: deviceKey = gamepad GUID string or kKeyboardDevice. Falls back to the built-in profile of
  // the kind when nothing (valid) is assigned.
  QString assignedProfileId(const QString& deviceKey, const QString& kind) const;
  bool assign(const QString& deviceKey, const QString& profileId);

 private:
  bool save() const;
  QString uniqueName(const QString& base) const;

  QString path_;
  QJsonObject raw_;
  QList<ControllerProfile> user_;
  QMap<QString, QString> assignments_;
};

}  // namespace framebeam
