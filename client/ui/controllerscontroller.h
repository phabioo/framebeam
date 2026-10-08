#pragma once
// ControllersController: data and actions of the Controllers page (3f) and the bridge between devices and the game.
// Owns the SDL3 GamepadService and the local controller profiles (<data>/settings/controllers.json).
// Mapping chain: physical -> profile -> FrameBeam input -> system input profile -> libretro joypad (merged with the
// keyboard in GameSession). The mouse is the touch input (fixed, no profile).

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <optional>

#include "controllerprofiles.h"
#include "gamepadservice.h"

namespace framebeam::ui {

class ControllersController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")

  Q_PROPERTY(bool gamepadAvailable READ gamepadAvailable CONSTANT)  // SDL gamepad subsystem initialised
  Q_PROPERTY(QString gamepadNote READ gamepadNote CONSTANT)         // why gamepads are off, else empty
  // [{key, kind: gamepad|keyboard|mouse, name, slot, profile, connected, status}]
  Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
  Q_PROPERTY(QString selectedDevice READ selectedDevice NOTIFY selectionChanged)
  Q_PROPERTY(QVariantMap device READ device NOTIFY selectionChanged)  // selected device row
  // Profiles that fit the selected device (select model): [{value: id, label: name, builtin}]
  Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
  Q_PROPERTY(QString profileId READ profileId NOTIFY profilesChanged)
  Q_PROPERTY(QString profileName READ profileName NOTIFY profilesChanged)
  Q_PROPERTY(bool profileBuiltin READ profileBuiltin NOTIFY profilesChanged)
  // [{input, label, target, binding, mapped, changed, listening}]; empty for the mouse
  Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
  Q_PROPERTY(int changedCount READ changedCount NOTIFY rowsChanged)  // bindings that differ from the default profile
  Q_PROPERTY(QString listening READ listening NOTIFY rowsChanged)  // input id waiting for "Press a button…"
  Q_PROPERTY(bool supportsLid READ supportsLid CONSTANT)           // no input profile has a lid input yet
  // Player hotkeys (global, independent of the selected device): [{action, label, key, set, changed, listening, fixed,
  // conflictInput}]. The last row is the fixed Esc row (fixed = true, no capture). conflictInput = label of the
  // keyboard-profile input mapped to the same key ("" = none; the hotkey wins).
  Q_PROPERTY(QVariantList hotkeyRows READ hotkeyRows NOTIFY hotkeysChanged)
  Q_PROPERTY(int hotkeysChangedCount READ hotkeysChangedCount NOTIFY hotkeysChanged)
  Q_PROPERTY(QString hotkeyListening READ hotkeyListening NOTIFY hotkeysChanged)  // action id waiting for "Press a key…"
  Q_PROPERTY(QString hotkeyNote READ hotkeyNote NOTIFY hotkeysChanged)            // "Already used by <label>" after a rejected key
  // {actionId: key label} for the key hints; "" = unassigned (no hint).
  Q_PROPERTY(QVariantMap hotkeyLabels READ hotkeyLabels NOTIFY hotkeysChanged)
  // System-specific labels from the system manifest (column header, touch input name).
  Q_PROPERTY(QString systemLabel READ systemLabel NOTIFY labelsChanged)
  Q_PROPERTY(QString touchLabel READ touchLabel NOTIFY labelsChanged)
  // Input test: ids of the FrameBeam inputs that are active on the selected device.
  Q_PROPERTY(QStringList activeInputs READ activeInputs NOTIFY testChanged)
  // Button labels (glyph sets, design 3f-2/3f-3). labelChoice = saved choice of the selected gamepad ("auto" | "xbox" |
  // "playstation" | "generic"; saved per physical device), labelSet = effective set id (adds "keyboard" for the keyboard),
  // labelSetName = its title ("Xbox"), labelChoices = select model [{value, label}] ("Auto (PlayStation)" first).
  Q_PROPERTY(QString labelChoice READ labelChoice NOTIFY labelSetChanged)
  Q_PROPERTY(QString labelSet READ labelSet NOTIFY labelSetChanged)
  Q_PROPERTY(QString labelSetName READ labelSetName NOTIFY labelSetChanged)
  Q_PROPERTY(QVariantList labelChoices READ labelChoices NOTIFY labelSetChanged)
  // Cells of the controller-shaped input test: [{id, glyph, col, row, span, square, active}].
  Q_PROPERTY(QVariantList testCells READ testCells NOTIFY testChanged)

 public:
  ControllersController(const QString& dataDir, QObject* parent = nullptr);

  // Starts SDL gamepads (pollIntervalMs <= 0: no timer, tests call gamepads()->poll()). Without SDL: keyboard only.
  void start(bool enableGamepads = true, int pollIntervalMs = 8);
  input::GamepadService* gamepads() { return &pads_; }
  ControllerProfiles* profileStore() { return &profiles_; }
  bool gamepadAvailable() const { return pads_.available(); }
  QString gamepadNote() const { return note_; }

  QVariantList devices() const;
  QString selectedDevice() const { return selected_; }
  QVariantMap device() const;
  QVariantList profiles() const;
  QString profileId() const;
  QString profileName() const;
  bool profileBuiltin() const;
  QVariantList rows() const;
  QString listening() const { return listening_; }
  int changedCount() const;
  bool supportsLid() const { return false; }
  QStringList activeInputs() const;
  QString labelChoice() const;
  QString labelSet() const;
  QString labelSetName() const;
  QVariantList labelChoices() const;
  QVariantList testCells() const;
  QString systemLabel() const { return systemLabel_; }
  QString touchLabel() const { return touchLabel_; }
  void setSystemLabels(const QString& systemLabel, const QString& touchLabel);

  QVariantList hotkeyRows() const;
  int hotkeysChangedCount() const { return profiles_.hotkeysChangedCount(); }
  QString hotkeyListening() const { return hotkeyListening_; }
  QString hotkeyNote() const { return hotkeyNote_; }
  QVariantMap hotkeyLabels() const;
  QSet<int> hotkeyKeys() const;  // assigned hotkey keys (Esc not included)

  // Qt::Key -> joypad mask of the keyboard profile (nds system profile applied); keys that are hotkeys are left out.
  QHash<int, quint32> keyboardMap() const;

  Q_INVOKABLE void selectDevice(const QString& key);
  Q_INVOKABLE void setLabelChoice(const QString& choice);  // "auto" | "xbox" | "playstation" | "generic"; gamepads only
  Q_INVOKABLE void selectProfile(const QString& id);
  Q_INVOKABLE void duplicateProfile();
  Q_INVOKABLE void renameProfile(const QString& name);
  Q_INVOKABLE void deleteProfile();
  Q_INVOKABLE void resetProfile();
  Q_INVOKABLE void beginCapture(const QString& inputId);
  Q_INVOKABLE void cancelCapture();
  Q_INVOKABLE void clearBinding(const QString& inputId);
  Q_INVOKABLE void resetBinding(const QString& inputId);  // one input back to the default profile's binding
  // Keyboard: key while capturing (true = consumed; Escape cancels) / while testing (state of the tiles).
  Q_INVOKABLE bool captureKey(int qtKey);
  Q_INVOKABLE void testKey(int qtKey, bool pressed);
  Q_INVOKABLE void clearTestKeys();
  // Hotkeys. hotkeyAction: action id the Qt key triggers ("" = none; Esc is fixed and not an action).
  Q_INVOKABLE QString hotkeyAction(int qtKey) const { return profiles_.hotkeyAction(qtKey); }
  Q_INVOKABLE void beginHotkeyCapture(const QString& action);
  Q_INVOKABLE void cancelHotkeyCapture();
  Q_INVOKABLE void clearHotkey(const QString& action);
  Q_INVOKABLE void resetHotkey(const QString& action);
  Q_INVOKABLE void resetHotkeys();
  // "Duplicate to edit": duplicates the built-in profile, selects the copy and begins capture for the input on it.
  Q_INVOKABLE void duplicateAndCapture(const QString& inputId);

 signals:
  void devicesChanged();
  void selectionChanged();
  void profilesChanged();
  void rowsChanged();
  void testChanged();
  void keyboardMapChanged();
  void labelsChanged();
  void labelSetChanged();
  void hotkeysChanged();
  void libretroMaskChanged(quint32 mask);  // P1 gamepad, nds system profile

 private:
  QString systemLabel_;
  QString touchLabel_ = QStringLiteral("Touch");
  struct Selected {
    QString kind;      // gamepad | keyboard | mouse | ""
    QString deviceKey; // assignment key (guid or "keyboard")
    int padId = 0;
  };
  Selected selection() const;
  QString labelSetFor(const Selected& s, const input::PadDevice& d) const;
  std::optional<ControllerProfile> currentProfile() const;
  QString deviceKeyForAssignment() const;
  void profileChanged();   // after a change of profiles/assignments: tell the consumers
  void applyCapture(const QString& token);
  bool captureHotkey(int qtKey);
  void hotkeysUpdated();  // after a change of the hotkeys: consumers refresh (also the keyboard map)
  void ensureSelection();

  ControllerProfiles profiles_;
  input::GamepadService pads_;
  QString selected_ = QStringLiteral("keyboard");
  QString listening_;
  QString note_;
  QString hotkeyListening_;
  QString hotkeyNote_;
  QSet<int> heldKeys_;
};

}  // namespace framebeam::ui
