# framebeam_core: Player core without GUI (core/ + network/). Add sources here and in ../network.
add_library(framebeam_core STATIC version.cpp credentialstore.cpp profilestore.cpp romcache.cpp savestore.cpp playersettings.cpp firmwarecache.cpp corecache.cpp dsvsave.cpp emulationsettings.cpp controllerprofiles.cpp
  filelog.cpp semver.cpp updateindex.cpp updatesig.cpp installroot.cpp updater.cpp localhub.cpp hubsetup.cpp fsutil.cpp)
target_include_directories(framebeam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(framebeam_core PUBLIC cxx_std_20)
target_link_libraries(framebeam_core PUBLIC Qt6::Core Qt6::Concurrent Qt6::Network OpenSSL::Crypto)
if(WIN32)
  target_link_libraries(framebeam_core PUBLIC advapi32 shell32)
endif()
framebeam_warnings(framebeam_core)
# Build identity (see client/CMakeLists.txt): only version.cpp sees it, so a changed version rebuilds one file.
set_source_files_properties(version.cpp PROPERTIES COMPILE_DEFINITIONS
  "FRAMEBEAM_VERSION_STR=\"${FRAMEBEAM_EFFECTIVE_VERSION}\";FRAMEBEAM_CHANNEL_STR=\"${FRAMEBEAM_CHANNEL}\";FRAMEBEAM_COMMIT_STR=\"${FRAMEBEAM_COMMIT}\"")

framebeam_add_test(core_version tests/core_version_test.cpp LIBS framebeam_core)
framebeam_add_test(core_profilestore tests/profilestore_test.cpp LIBS framebeam_core)
framebeam_add_test(core_romcache tests/romcache_test.cpp LIBS framebeam_core)
framebeam_add_test(core_savestore tests/savestore_test.cpp LIBS framebeam_core)
framebeam_add_test(core_dsvsave tests/dsvsave_test.cpp LIBS framebeam_core)
framebeam_add_test(core_playersettings tests/playersettings_test.cpp LIBS framebeam_core)
framebeam_add_test(core_emulationsettings tests/emulationsettings_test.cpp LIBS framebeam_core)
framebeam_add_test(core_controllerprofiles tests/controllerprofiles_test.cpp LIBS framebeam_core)
framebeam_add_test(core_firmwarecache tests/firmwarecache_test.cpp LIBS framebeam_core)
framebeam_add_test(core_corecache tests/corecache_test.cpp LIBS framebeam_core)
framebeam_add_test(core_filelog tests/filelog_test.cpp LIBS framebeam_core)
framebeam_add_test(core_update tests/update_test.cpp LIBS framebeam_core)
framebeam_add_test(core_localhub tests/localhub_test.cpp LIBS framebeam_core)
framebeam_add_test(core_fsutil tests/fsutil_test.cpp LIBS framebeam_core)
if(WIN32)
  # Writes/reads/deletes a dummy value in the Windows Credential Manager (Windows CI only).
  framebeam_add_test(core_windows_credential_store tests/windows_credential_store_test.cpp LIBS framebeam_core)
endif()
