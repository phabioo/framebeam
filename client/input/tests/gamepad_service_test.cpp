// GamepadService headless with SDL virtual joysticks: hotplug, slots, press -> FrameBeam input -> libretro mask
// (default and remapped profile), analog stick/trigger, capture. No hardware, no video driver.
#include <SDL3/SDL.h>

#include <QSignalSpy>
#include <QtTest>

#include "controllerprofiles.h"
#include "gamepadservice.h"

using namespace framebeam;
using namespace framebeam::input;

namespace {
constexpr quint32 kB = 1u << 0, kY = 1u << 1, kSelect = 1u << 2, kStart = 1u << 3, kUp = 1u << 4, kDown = 1u << 5,
                  kLeft = 1u << 6, kRight = 1u << 7, kA = 1u << 8, kX = 1u << 9, kL = 1u << 10, kR = 1u << 11;

// SDL default mapping of virtual gamepads: buttons 0..14 = a b x y back guide start ls rs lb rb dpup dpdown dpleft dpright,
// axes 0..5 = leftx lefty rightx righty lefttrigger righttrigger.
enum Btn { South = 0, East, West, North, Back, Guide, Start, LS, RS, LB, RB, DUp, DDown, DLeft, DRight };
enum Axis { LeftX = 0, LeftY, RightX, RightY, LT, RT };

struct VirtualPad {
  SDL_JoystickID id = 0;
  SDL_Joystick* js = nullptr;
  explicit VirtualPad(const char* name) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6;
    desc.nbuttons = 15;
    desc.name = name;
    static Uint16 product = 0x4000;  // distinct GUIDs: SDL derives the gamepad mapping (and its name) from the GUID
    desc.vendor_id = 0x1209;
    desc.product_id = product++;
    id = SDL_AttachVirtualJoystick(&desc);
    if (id != 0) js = SDL_OpenJoystick(id);
  }
  ~VirtualPad() {
    if (js) SDL_CloseJoystick(js);
    if (id != 0) SDL_DetachVirtualJoystick(id);
  }
  void button(int b, bool down) { SDL_SetJoystickVirtualButton(js, b, down); }
  void axis(int a, int v) { SDL_SetJoystickVirtualAxis(js, a, static_cast<Sint16>(v)); }
};
}  // namespace

class GamepadServiceTest : public QObject {
  Q_OBJECT
 private slots:
  void hotplugSlotsAndDefaultMapping() {
    GamepadService svc;
    QVERIFY2(svc.start(0), qPrintable(svc.error()));
    QVERIFY(svc.available());
    QVERIFY(svc.devices().isEmpty());
    QCOMPARE(svc.libretroMask(), 0u);

    QSignalSpy devSpy(&svc, &GamepadService::devicesChanged);
    QSignalSpy maskSpy(&svc, &GamepadService::libretroMaskChanged);
    VirtualPad p1("FrameBeam test pad 1");
    QVERIFY(p1.js != nullptr);
    svc.poll();
    QCOMPARE(svc.devices().size(), 1);
    QCOMPARE(svc.devices().first().slot, 1);
    QCOMPARE(svc.devices().first().name, QStringLiteral("FrameBeam test pad 1"));
    QVERIFY(!svc.devices().first().key.isEmpty());
    QVERIFY(devSpy.count() >= 1);

    // Default profile "Standard Gamepad": east = A, south = B, north = X, west = Y, shoulders = L/R, ...
    const struct { int btn; quint32 mask; } buttons[] = {{East, kA}, {South, kB}, {North, kX}, {West, kY}, {LB, kL}, {RB, kR},
                                                         {Start, kStart}, {Back, kSelect}, {DUp, kUp}, {DDown, kDown},
                                                         {DLeft, kLeft}, {DRight, kRight}};
    for (const auto& b : buttons) {
      p1.button(b.btn, true);
      svc.poll();
      QCOMPARE(svc.libretroMask(), b.mask);
      p1.button(b.btn, false);
      svc.poll();
      QCOMPARE(svc.libretroMask(), 0u);
    }
    QVERIFY(maskSpy.count() >= 24);

    // Several buttons at once; unmapped buttons (guide, stick press) do nothing.
    p1.button(East, true);
    p1.button(DUp, true);
    p1.button(LS, true);
    svc.poll();
    QCOMPARE(svc.libretroMask(), kA | kUp);
    p1.button(East, false);
    p1.button(DUp, false);
    p1.button(LS, false);

    // Left stick as D-pad (with dead zone) and triggers as buttons.
    p1.axis(LeftX, -3000);
    svc.poll();
    QCOMPARE(svc.libretroMask(), 0u);
    p1.axis(LeftX, -30000);
    p1.axis(LeftY, 30000);
    svc.poll();
    QCOMPARE(svc.libretroMask(), kLeft | kDown);
    p1.axis(LeftX, 0);
    p1.axis(LeftY, -30000);
    svc.poll();
    QCOMPARE(svc.libretroMask(), kUp);
    p1.axis(LeftY, 0);
    p1.axis(RT, 30000);
    svc.poll();
    QCOMPARE(svc.libretroMask(), kR);
    p1.axis(RT, -32768);  // trigger rest position is the axis minimum
    svc.poll();
    QCOMPARE(svc.libretroMask(), 0u);

    // Second pad: no slot; its input does not reach the game. When P1 leaves, the second pad becomes P1.
    VirtualPad p2("FrameBeam test pad 2");
    svc.poll();
    QCOMPARE(svc.devices().size(), 2);
    QCOMPARE(svc.devices().at(1).slot, 0);
    p2.button(East, true);
    svc.poll();
    QCOMPARE(svc.libretroMask(), 0u);
    QCOMPARE(svc.inputMaskFor(svc.devices().at(1).id), 1u << inputIndex(QStringLiteral("a")));
    p1.button(East, true);
    svc.poll();
    QCOMPARE(svc.libretroMask(), kA);
    p1.button(East, false);
    SDL_CloseJoystick(p1.js);
    p1.js = nullptr;
    SDL_DetachVirtualJoystick(p1.id);
    p1.id = 0;
    svc.poll();
    QCOMPARE(svc.devices().size(), 1);
    QCOMPARE(svc.devices().first().name, QStringLiteral("FrameBeam test pad 2"));
    QCOMPARE(svc.devices().first().slot, 1);
    QCOMPARE(svc.libretroMask(), kA);  // pad 2 still holds east
  }

  void remappedProfile() {
    GamepadService svc;
    QVERIFY2(svc.start(0), qPrintable(svc.error()));
    Bindings custom = ControllerProfiles::builtinProfile(QStringLiteral("gamepad")).bindings;
    custom[QStringLiteral("a")] = {padToken(QStringLiteral("x"))};            // west button = A
    custom[QStringLiteral("b")] = {};                                          // south = not mapped
    custom[QStringLiteral("y")] = {};
    custom[QStringLiteral("l")] = {padToken(QStringLiteral("lefttrigger"))};
    QString askedKey;
    svc.setBindingsProvider([&](const QString& key) {
      askedKey = key;
      return custom;
    });
    VirtualPad pad("FrameBeam test pad");
    svc.poll();
    QCOMPARE(svc.devices().size(), 1);
    QCOMPARE(askedKey, svc.devices().first().key);

    pad.button(West, true);
    svc.poll();
    QCOMPARE(svc.libretroMask(), kA);
    pad.button(West, false);
    pad.button(East, true);  // formerly A, now unmapped
    pad.button(South, true);
    svc.poll();
    QCOMPARE(svc.libretroMask(), 0u);
    pad.button(East, false);
    pad.button(South, false);
    pad.button(LB, true);  // L is now only the trigger
    svc.poll();
    QCOMPARE(svc.libretroMask(), 0u);
    pad.button(LB, false);
    pad.axis(LT, 32000);
    svc.poll();
    QCOMPARE(svc.libretroMask(), kL);
    pad.axis(LT, -32768);
    svc.poll();

    // Profile change at runtime.
    custom[QStringLiteral("x")] = {padToken(QStringLiteral("rightstick"))};
    svc.invalidateBindings();
    pad.button(RS, true);
    svc.poll();
    QCOMPARE(svc.libretroMask(), kX);
  }

  void capture() {
    GamepadService svc;
    QVERIFY2(svc.start(0), qPrintable(svc.error()));
    VirtualPad pad("FrameBeam test pad");
    svc.poll();
    QSignalSpy spy(&svc, &GamepadService::captured);
    pad.button(North, true);  // already held when the capture starts: ignored
    svc.poll();
    svc.startCapture();
    QVERIFY(svc.capturing());
    svc.poll();
    QCOMPARE(spy.count(), 0);
    pad.button(LB, true);
    svc.poll();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("pad:leftshoulder"));
    QVERIFY(!svc.capturing());  // reported once
    svc.poll();
    QCOMPARE(spy.count(), 1);
    pad.button(LB, false);
    pad.button(North, false);

    // Stick direction and trigger can be captured too; cancel stops the capture.
    svc.startCapture();
    pad.axis(LeftX, 30000);
    svc.poll();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.last().at(0).toString(), QStringLiteral("pad:leftx+"));
    pad.axis(LeftX, 0);
    svc.poll();
    svc.startCapture();
    svc.cancelCapture();
    pad.button(East, true);
    svc.poll();
    QCOMPARE(spy.count(), 2);
  }

  void unavailableWithoutSdlIsGraceful() {
    // A service that was never started reports "unavailable" and does nothing.
    GamepadService svc;
    QVERIFY(!svc.available());
    svc.poll();
    QVERIFY(svc.devices().isEmpty());
    QCOMPARE(svc.libretroMask(), 0u);
  }
};

QTEST_GUILESS_MAIN(GamepadServiceTest)
#include "gamepad_service_test.moc"
