# framebeam_player_launcher (Windows only): the tiny top-level framebeam_player.exe of the installed layout
#   {app}\framebeam_player.exe  ->  starts {app}\bin\framebeam_player.exe with the same arguments.
# No Qt, static CRT, GUI subsystem (no console window, like the Player; passes its std handles to the child explicitly and returns the exit code of the child).
# Output name is framebeam_player, so it is built into its own directory and never overwrites the real Player:
#   <build>/launcher/framebeam_player.exe   (CI copies it to the package top level)
if(WIN32)
  add_executable(framebeam_player_launcher WIN32 launcher.cpp launcher.rc)
  # launcher.rc references the Player icon (../app/icons/player.ico); rc.exe resolves it via the include path, independent of the working directory.
  target_include_directories(framebeam_player_launcher PRIVATE $<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}/../app>)
  set_target_properties(framebeam_player_launcher PROPERTIES
    OUTPUT_NAME framebeam_player
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/launcher
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_definitions(framebeam_player_launcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(framebeam_player_launcher PRIVATE user32)
  target_link_libraries(framebeam_player_launcher PRIVATE shell32)
  framebeam_warnings(framebeam_player_launcher)
  if(MSVC)
    # Per-config subfolders of multi-config generators would hide the path above; keep it flat.
    foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
      set_target_properties(framebeam_player_launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/launcher)
    endforeach()
  endif()
endif()

framebeam_add_test(launcher_cmdline tests/cmdline_test.cpp)
target_include_directories(test_launcher_cmdline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
