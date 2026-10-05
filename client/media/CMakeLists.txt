# framebeam_media_deps: INTERFACE target with the Phase 4 media dependencies on both platforms
# (LibDataChannel, FFmpeg avcodec/swscale/avutil, Opus). Sources of the media path are added here later.
#  Linux:   libdatachannel from scripts/fetch-libdatachannel.sh (CMAKE_PREFIX_PATH / LibDataChannel_ROOT),
#           FFmpeg and Opus from apt via pkg-config.
#  Windows: everything from vcpkg (client/vcpkg.json).
add_library(framebeam_media_deps INTERFACE)

find_package(LibDataChannel CONFIG REQUIRED)
target_link_libraries(framebeam_media_deps INTERFACE LibDataChannel::LibDataChannel)

if(WIN32)
  find_package(FFMPEG REQUIRED)  # vcpkg: FFMPEG_INCLUDE_DIRS / FFMPEG_LIBRARIES (avcodec, swscale, avutil)
  find_package(Opus CONFIG REQUIRED)
  target_include_directories(framebeam_media_deps INTERFACE ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(framebeam_media_deps INTERFACE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(framebeam_media_deps INTERFACE ${FFMPEG_LIBRARIES} Opus::opus)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FB_FFMPEG REQUIRED IMPORTED_TARGET libavcodec libswscale libavutil)
  pkg_check_modules(FB_OPUS REQUIRED IMPORTED_TARGET opus)
  target_link_libraries(framebeam_media_deps INTERFACE PkgConfig::FB_FFMPEG PkgConfig::FB_OPUS)
endif()

framebeam_add_test(media_deps tests/media_deps_test.cpp LIBS framebeam_media_deps)
