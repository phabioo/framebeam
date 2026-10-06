// Headless SDL3 check: gamepad subsystem init, virtual joystick attach/open/button/axis, quit.
#include <SDL3/SDL.h>

#include <cstdio>

static int fail(const char *what)
{
    std::fprintf(stderr, "[sdl3] %s: %s\n", what, SDL_GetError());
    SDL_Quit();
    return 1;
}

int main()
{
    if (!SDL_Init(SDL_INIT_GAMEPAD))
        return fail("SDL_Init(SDL_INIT_GAMEPAD)");

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6;
    desc.nbuttons = 15;
    desc.name = "FrameBeam test pad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0)
        return fail("SDL_AttachVirtualJoystick");

    SDL_Joystick *js = SDL_OpenJoystick(id);
    if (!js)
        return fail("SDL_OpenJoystick");
    if (!SDL_SetJoystickVirtualButton(js, 0, true) || !SDL_SetJoystickVirtualAxis(js, 0, 16000))
        return fail("virtual input");
    SDL_UpdateJoysticks();
    const bool ok = SDL_GetJoystickButton(js, 0) && SDL_GetJoystickAxis(js, 0) == 16000;

    SDL_CloseJoystick(js);
    SDL_DetachVirtualJoystick(id);
    SDL_Quit();
    if (!ok) {
        std::fprintf(stderr, "[sdl3] virtual joystick state not reflected\n");
        return 1;
    }
    return 0;
}
