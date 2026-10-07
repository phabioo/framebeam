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
  // [{key, kind: gamepad|keyboard|mouse, name, slot, profile}]
  Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
  Q_PROPERTY(QString selectedDevice READ selectedDevice NOTIFY selectionChanged)
  Q_PROPERTY(QVariantMap device READ device NOTIFY selectionChanged)  // selected device row
  // Profiles that fit the selected device (select model): [{value: id, label: name, builtin}]
  Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
  Q_PROPERTY(QString profileId READ profileId NOTIFY profilesChanged)
  Q_PROPERTY(QString profileName READ profileName NOTIFY profilesChanged)
  Q_PROPERTY(bool profileBuiltin READ profileBuiltin NOTIFY profilesChanged)
  // [{input, label, target, binding, mapped, listening}]; empty for the mouse
  Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
  Q_PROPERTY(QString listening READ listening NOTIFY rowsChanged)  // input id waiting for "Press a button…"
  Q_PROPERTY(bool supportsLid READ supportsLid CONSTANT)           // no input profile has a lid input yet
  // System-specific labels from the system manifest (column header, touch input name).
  Q_PROPERTY(QString systemLabel READ systemLabel NOTIFY labelsChanged)
  Q_PROPERTY(QString touchLabel READ touchLabel NOTIFY labelsChanged)
  // Input test: ids of the FrameBeam inputs that are active on the selected device.
  Q_PROPERTY(QStringList activeInputs READ activeInputs NOTIFY testChanged)

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
  bool supportsLid() const { return false; }
  QStringList activeInputs() const;
  QString systemLabel() const { return systemLabel_; }
  QString touchLabel() const { return touchLabel_; }
  void setSystemLabels(const QString& systemLabel, const QString& touchLabel);

  // Qt::Key -> joypad mask of the keyboard profile (nds system profile applied).
  QHash<int, quint32> keyboardMap() const;

  Q_INVOKABLE void selectDevice(const QString& key);
  Q_INVOKABLE void selectProfile(const QString& id);
  Q_INVOKABLE void duplicateProfile();
  Q_INVOKABLE void renameProfile(const QString& name);
  Q_INVOKABLE void deleteProfile();
  Q_INVOKABLE void resetProfile();
  Q_INVOKABLE void beginCapture(const QString& inputId);
  Q_INVOKABLE void cancelCapture();
  Q_INVOKABLE void clearBinding(const QString& inputId);
  // Keyboard: key while capturing (true = consumed; Escape cancels) / while testing (state of the tiles).
  Q_INVOKABLE bool captureKey(int qtKey);
  Q_INVOKABLE void testKey(int qtKey, bool pressed);
  Q_INVOKABLE void clearTestKeys();

 signals:
  void devicesChanged();
  void selectionChanged();
  void profilesChanged();
  void rowsChanged();
  void testChanged();
  void keyboardMapChanged();
  void labelsChanged();
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
  std::optional<ControllerProfile> currentProfile() const;
  QString deviceKeyForAssignment() const;
  void profileChanged();   // after a change of profiles/assignments: tell the consumers
  void applyCapture(const QString& token);
  void ensureSelection();

  ControllerProfiles profiles_;
  input::GamepadService pads_;
  QString selected_ = QStringLiteral("keyboard");
  QString listening_;
  QString note_;
  QSet<int> heldKeys_;
};

}  // namespace framebeam::ui
