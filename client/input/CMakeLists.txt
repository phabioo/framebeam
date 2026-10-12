# framebeam_sdl3: INTERFACE target for SDL3 (>= 3.2, gamepad/joystick subsystem only; audio stays on Qt Multimedia).
# Link it from the ui/input code: target_link_libraries(<target> PRIVATE framebeam_sdl3), include <SDL3/SDL.h>.
#  Linux:   scripts/fetch-sdl3.sh (pinned source build, no video/audio), found via CMAKE_PREFIX_PATH.
#  Windows: vcpkg port sdl3 without features (client/vcpkg.json); SDL3.dll is bundled by the package step in CI.
add_library(framebeam_sdl3 INTERFACE)

find_package(SDL3 3.2 CONFIG REQUIRED)
target_link_libraries(framebeam_sdl3 INTERFACE SDL3::SDL3)

# framebeam_input: gamepad service (SDL3 hotplug, state, capture) and the mapping chain
# physical binding -> FrameBeam input -> system profile nds -> libretro joypad mask.
# framebeam_inputmap: the input profile tables (inputmap.cpp), filled by ManifestRegistry; shared by emulation and input
# (small on purpose: no SDL3).
add_library(framebeam_inputmap STATIC inputmap.cpp)
target_include_directories(framebeam_inputmap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(framebeam_inputmap PUBLIC cxx_std_20)
target_link_libraries(framebeam_inputmap PUBLIC framebeam_core Qt6::Core)
framebeam_warnings(framebeam_inputmap)

add_library(framebeam_input STATIC gamepadservice.cpp padhotkeys.cpp padtype.cpp)
target_include_directories(framebeam_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(framebeam_input PUBLIC cxx_std_20)
target_link_libraries(framebeam_input PUBLIC framebeam_inputmap framebeam_core Qt6::Core)
target_link_libraries(framebeam_input PRIVATE framebeam_sdl3)
framebeam_warnings(framebeam_input)

# Proves SDL3 links and runs headless: SDL_INIT_GAMEPAD + a virtual joystick (no hardware, no video driver).
framebeam_add_test(input_sdl3 tests/sdl3_virtual_joystick_test.cpp LIBS framebeam_sdl3)
# GamepadService with SDL virtual joysticks: hotplug, press -> FrameBeam input -> joypad mask (default/remapped), capture.
framebeam_add_test(input_gamepad tests/gamepad_service_test.cpp LIBS framebeam_input framebeam_emulation framebeam_sdl3)
# Controller type detection (SDL gamepad type / vendor ids -> Xbox, PlayStation, Generic), pure + virtual joysticks.
framebeam_add_test(input_padtype tests/padtype_test.cpp LIBS framebeam_input framebeam_sdl3)
# Gamepad hotkey detection (pure), plus the GamepadService integration with SDL virtual joysticks.
framebeam_add_test(input_padhotkeys tests/padhotkeys_test.cpp LIBS framebeam_input framebeam_emulation framebeam_sdl3)
# Input profile tables from the system manifests: nds/3ds masks unchanged, gb/gba, conflicts, unknown targets.
framebeam_add_test(input_profiles tests/inputprofile_test.cpp LIBS framebeam_input framebeam_emulation)
