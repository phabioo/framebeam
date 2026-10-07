# framebeam_player_launcher (Windows only): the tiny top-level framebeam_player.exe of the installed layout
#   {app}\framebeam_player.exe  ->  starts {app}\bin\framebeam_player.exe with the same arguments.
# No Qt, static CRT, console subsystem (inherits the std handles of its caller, returns the exit code of the child).
# Output name is framebeam_player, so it is built into its own directory and never overwrites the real Player:
#   <build>/launcher/framebeam_player.exe   (CI copies it to the package top level)
add_executable(framebeam_player_launcher launcher.cpp)
set_target_properties(framebeam_player_launcher PROPERTIES
  OUTPUT_NAME framebeam_player
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/launcher
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(framebeam_player_launcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(framebeam_player_launcher PRIVATE shell32)
framebeam_warnings(framebeam_player_launcher)
if(MSVC)
  # Per-config subfolders of multi-config generators would hide the path above; keep it flat.
  foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(framebeam_player_launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/launcher)
  endforeach()
endif()
