# framebeam_sdl3: INTERFACE target for SDL3 (>= 3.2, gamepad/joystick subsystem only; audio stays on Qt Multimedia).
# Link it from the ui/input code: target_link_libraries(<target> PRIVATE framebeam_sdl3), include <SDL3/SDL.h>.
#  Linux:   scripts/fetch-sdl3.sh (pinned source build, no video/audio), found via CMAKE_PREFIX_PATH.
#  Windows: vcpkg port sdl3 without features (client/vcpkg.json); SDL3.dll is bundled by the package step in CI.
add_library(framebeam_sdl3 INTERFACE)

find_package(SDL3 3.2 CONFIG REQUIRED)
target_link_libraries(framebeam_sdl3 INTERFACE SDL3::SDL3)

# Proves SDL3 links and runs headless: SDL_INIT_GAMEPAD + a virtual joystick (no hardware, no video driver).
framebeam_add_test(input_sdl3 tests/sdl3_virtual_joystick_test.cpp LIBS framebeam_sdl3)
