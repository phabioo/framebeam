# uzume_launcher (Windows only): the tiny top-level Uzume.exe of the installed layout
#   {app}\Uzume.exe  ->  starts {app}\bin\Uzume.exe with the same arguments.
# No Qt, static CRT, GUI subsystem (no console window, like the Player; passes its std handles to the child explicitly and returns the exit code of the child).
# Output name is Uzume, so it is built into its own directory and never overwrites the real app:
#   <build>/launcher/Uzume.exe   (CI copies it to the package top level)
if(WIN32)
  add_executable(uzume_launcher WIN32 launcher.cpp launcher.rc)
  # launcher.rc references the Player icon (../app/icons/player.ico); rc.exe resolves it via the include path, independent of the working directory.
  target_include_directories(uzume_launcher PRIVATE $<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}/../app>)
  set_target_properties(uzume_launcher PROPERTIES
    OUTPUT_NAME Uzume
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/launcher
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_definitions(uzume_launcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(uzume_launcher PRIVATE user32)
  target_link_libraries(uzume_launcher PRIVATE shell32 bcrypt)
  uzume_warnings(uzume_launcher)
  if(MSVC)
    # Same application manifest as the Player (longPathAware, UTF-8 active code page), merged by CMake into the linker's manifest.
    target_sources(uzume_launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app/uzume.manifest)
    # Per-config subfolders of multi-config generators would hide the path above; keep it flat.
    foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
      set_target_properties(uzume_launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/launcher)
    endforeach()
  endif()
endif()

uzume_add_test(launcher_cmdline tests/cmdline_test.cpp)
target_include_directories(test_launcher_cmdline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
uzume_add_test(launcher_msiupdate tests/msiupdate_test.cpp)
target_include_directories(test_launcher_msiupdate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
