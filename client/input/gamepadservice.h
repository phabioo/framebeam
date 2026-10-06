#pragma once
// GamepadService: SDL3 gamepads (SDL_INIT_GAMEPAD only, no SDL video) pumped from the Qt event loop.
//
// Hotplug (SDL_EVENT_GAMEPAD_ADDED/REMOVED), button/axis state, mapping to FrameBeam inputs via the binding
// tokens of the controller profile of each device, and a "press a button" capture for remapping.
// The first connected gamepad is player 1 (P1); the DS has one player, further pads are listed without a slot.
// If SDL cannot be initialised the service stays unavailable (available() == false) and the Player runs
// with keyboard and mouse only.

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <functional>

#include "inputmap.h"

namespace framebeam::input {

struct PadDevice {
  int id = 0;       // SDL_JoystickID (instance id, changes on every connect)
  QString name;
  QString key;      // stable key for profile assignment (SDL GUID string)
  int slot = 0;     // 1 = P1, 0 = no slot
};

class GamepadService : public QObject {
  Q_OBJECT
 public:
  explicit GamepadService(QObject* parent = nullptr);
  ~GamepadService() override;

  // Initialises SDL_INIT_GAMEPAD and starts the poll timer (pollIntervalMs <= 0: no timer, call poll() yourself).
  bool start(int pollIntervalMs = 8);
  bool available() const { return available_; }
  QString error() const { return error_; }

  QList<PadDevice> devices() const;
  PadDevice device(int id) const;  // id 0 / unknown: default-constructed

  // Bindings of the profile that is assigned to a device (keyed by PadDevice::key); built-in gamepad profile if unset.
  using BindingsProvider = std::function<Bindings(const QString& deviceKey)>;
  void setBindingsProvider(BindingsProvider provider);
  void invalidateBindings();  // profile or assignment changed

  quint32 inputMask() const { return p1InputMask_; }        // FrameBeam inputs of P1
  quint32 libretroMask() const { return p1Libretro_; }      // nds system profile applied
  quint32 inputMaskFor(int deviceId) const;                 // FrameBeam inputs of one device (input test)
  QSet<QString> pressedTokens(int deviceId) const;          // raw tokens currently active on one device

  // Capture: the next newly pressed token (button, D-pad, trigger or stick direction) of any pad is reported once.
  void startCapture();
  void cancelCapture();
  bool capturing() const { return capturing_; }

  // Reads SDL events and the current state; normally driven by the timer.
  void poll();

 signals:
  void devicesChanged();
  void stateChanged();                          // pressed tokens of any device changed (input test)
  void libretroMaskChanged(quint32 mask);       // P1 mask (nds system profile) changed
  void captured(const QString& token, int deviceId);

 private:
  struct Dev;
  Dev* find(int id);
  const Dev* find(int id) const;
  void openDevice(int id);
  void closeDevice(int id);
  void refreshSlots();
  QSet<QString> readTokens(Dev& d) const;
  const QHash<QString, quint32>& compiled(const Dev& d) const;

  bool available_ = false;
  QString error_;
  QTimer timer_;
  QList<Dev*> devs_;  // connection order
  BindingsProvider provider_;
  mutable QHash<QString, QHash<QString, quint32>> compiledByKey_;
  quint32 p1InputMask_ = 0;
  quint32 p1Libretro_ = 0;
  bool capturing_ = false;
  QSet<QString> captureBaseline_;
  bool sdlInited_ = false;
};

}  // namespace framebeam::input
