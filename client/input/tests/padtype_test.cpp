// Controller type detection for the "Button labels" glyph set: pure mapping (SDL gamepad type, vendor ids) and
// SDL virtual joysticks that look like Xbox, PlayStation and Nintendo pads. No hardware, no video driver.
#include <SDL3/SDL.h>

#include <QtTest>

#include "gamepadservice.h"
#include "padtype.h"

using namespace framebeam::input;

namespace {
struct VirtualPad {
  SDL_JoystickID id = 0;
  SDL_Joystick* js = nullptr;
  VirtualPad(const char* name, Uint16 vendor, Uint16 product) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6;
    desc.nbuttons = 15;
    desc.name = name;
    desc.vendor_id = vendor;
    desc.product_id = product;
    id = SDL_AttachVirtualJoystick(&desc);
    if (id != 0) js = SDL_OpenJoystick(id);
  }
  ~VirtualPad() {
    if (js) SDL_CloseJoystick(js);
    if (id != 0) SDL_DetachVirtualJoystick(id);
  }
};
}  // namespace

class PadTypeTest : public QObject {
  Q_OBJECT
 private slots:
  void sdlTypesMapToSets() {
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_XBOX360, 0, 0), PadType::Xbox);
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_XBOXONE, 0, 0), PadType::Xbox);
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_PS3, 0, 0), PadType::PlayStation);
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_PS4, 0, 0), PadType::PlayStation);
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_PS5, 0, 0), PadType::PlayStation);
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0x057e, 0x2009), PadType::Generic);
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT, 0x057e, 0x2006), PadType::Generic);
  }
  void vendorFallbackForStandardAndUnknown() {
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_STANDARD, 0x045e, 0x1234), PadType::Xbox);   // Microsoft
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_UNKNOWN, 0x054c, 0x1234), PadType::PlayStation);  // Sony
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_STANDARD, 0x1209, 0x5100), PadType::Generic);
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_UNKNOWN, 0, 0), PadType::Generic);
    // A known type wins over the vendor (a Nintendo-layout pad from an Xbox-branded vendor stays Generic).
    QCOMPARE(classifyPad(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0x045e, 0), PadType::Generic);
  }
  void idsAndNames() {
    for (PadType t : {PadType::Xbox, PadType::PlayStation, PadType::Generic}) QCOMPARE(padTypeFromId(padTypeId(t)), t);
    QCOMPARE(padTypeFromId(QStringLiteral("auto"), PadType::Xbox), PadType::Xbox);
    QCOMPARE(padTypeName(PadType::PlayStation), QStringLiteral("PlayStation"));
    QCOMPARE(padTypeName(PadType::Xbox), QStringLiteral("Xbox"));
    QCOMPARE(padTypeName(PadType::Generic), QStringLiteral("Generic"));
  }
  void serviceDetectsVirtualPads() {
    GamepadService svc;
    QVERIFY2(svc.start(0), qPrintable(svc.error()));
    VirtualPad xbox("Test Xbox One Pad", 0x045e, 0x02ea);
    VirtualPad ds("Test DualSense", 0x054c, 0x0ce6);
    VirtualPad ds4("Test DualShock 4", 0x054c, 0x09cc);
    VirtualPad other("Test Generic Pad", 0x1209, 0x5100);
    VirtualPad nin("Test Switch Pro", 0x057e, 0x2009);
    QVERIFY(xbox.js && ds.js && ds4.js && other.js && nin.js);
    svc.poll();
    QCOMPARE(svc.devices().size(), 5);
    // Connection order = attach order (SDL may rename pads whose GUID it knows, so names are not compared).
    const QList<PadDevice> devs = svc.devices();
    QCOMPARE(devs[0].type, PadType::Xbox);
    QCOMPARE(devs[1].type, PadType::PlayStation);
    QCOMPARE(devs[2].type, PadType::PlayStation);
    QCOMPARE(devs[3].type, PadType::Generic);
    QCOMPARE(devs[4].type, PadType::Generic);
  }
};

QTEST_GUILESS_MAIN(PadTypeTest)
#include "padtype_test.moc"
