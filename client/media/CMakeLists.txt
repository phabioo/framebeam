# framebeam_media_deps: INTERFACE target with the media dependencies on both platforms
# (LibDataChannel, FFmpeg avcodec/swscale/avutil, Opus).
#  Linux:   libdatachannel from scripts/fetch-libdatachannel.sh (CMAKE_PREFIX_PATH / LibDataChannel_ROOT),
#           FFmpeg and Opus from apt via pkg-config.
#  Windows: everything from vcpkg (client/vcpkg.json).
add_library(framebeam_media_deps INTERFACE)

find_package(LibDataChannel CONFIG REQUIRED)
target_link_libraries(framebeam_media_deps INTERFACE LibDataChannel::LibDataChannel)

if(WIN32)
  # vcpkg's FFMPEG_LIBRARIES does not reliably contain swscale: find every library explicitly.
  find_package(FFMPEG REQUIRED)
  set(FB_FFMPEG_WIN_LIBS "")
  foreach(_comp avcodec avutil swscale)
    find_library(FB_FFMPEG_${_comp}_LIB NAMES ${_comp}
      HINTS ${FFMPEG_LIBRARY_DIRS} ${FFMPEG_ROOT_DIR}/lib
      PATH_SUFFIXES lib)
    if(NOT FB_FFMPEG_${_comp}_LIB)
      message(FATAL_ERROR "FFmpeg library '${_comp}' not found (vcpkg ffmpeg)")
    endif()
    list(APPEND FB_FFMPEG_WIN_LIBS ${FB_FFMPEG_${_comp}_LIB})
  endforeach()
  find_package(Opus CONFIG REQUIRED)
  target_include_directories(framebeam_media_deps INTERFACE ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(framebeam_media_deps INTERFACE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(framebeam_media_deps INTERFACE ${FB_FFMPEG_WIN_LIBS} Opus::opus)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FB_FFMPEG REQUIRED IMPORTED_TARGET libavcodec libswscale libavutil)
  pkg_check_modules(FB_OPUS REQUIRED IMPORTED_TARGET opus)
  target_link_libraries(framebeam_media_deps INTERFACE PkgConfig::FB_FFMPEG PkgConfig::FB_OPUS)
endif()

framebeam_add_test(media_deps tests/media_deps_test.cpp LIBS framebeam_media_deps)

# Audio helper without media dependencies (also used by framebeam_ui for the output resampler).
add_library(framebeam_audioutil STATIC audioresampler.cpp)
target_include_directories(framebeam_audioutil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(framebeam_audioutil PUBLIC Qt6::Core)
framebeam_warnings(framebeam_audioutil)

# framebeam_media: Session media path (ADR 0006 D5): H.264/Opus encode and decode, SessionHost (owner) and
# SessionViewer. Signaling is decoupled (Qt signals/slots), the Hub clients live in framebeam_core (network/).
add_library(framebeam_media STATIC
  bitratecontroller.cpp
  videoencoder.cpp
  videodecoder.cpp
  cudadriver.cpp
  cudaglcapture.cpp
  opuscodec.cpp
  opusdepacketizer.cpp
  mediacaps.cpp
  sessionhost.cpp
  sessionviewer.cpp
)
target_include_directories(framebeam_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(framebeam_media PUBLIC framebeam_core framebeam_audioutil framebeam_media_deps Qt6::Gui)
target_link_libraries(framebeam_media PRIVATE ${CMAKE_DL_LIBS})  # cudadriver.cpp: dlopen("libcuda.so.1") (ADR 0019)
framebeam_warnings(framebeam_media)

framebeam_add_test(media_viewer_reports tests/viewerreports_test.cpp LIBS framebeam_media)
framebeam_add_test(media_opus_depacketizer tests/opusdepacketizer_test.cpp LIBS framebeam_media)
framebeam_add_test(media_loopback tests/loopback_test.cpp LIBS framebeam_media)
framebeam_add_test(media_bitrate tests/bitratecontroller_test.cpp LIBS framebeam_media)
framebeam_add_test(media_rtcutil tests/rtcutil_test.cpp LIBS framebeam_media)

# GPU-direct encoding (ADR 0019): CUDA driver loader, CudaGlCapture and the CUDA input of VideoEncoder.
# media_cuda: pure (loader, error codes, encoder helpers). media_cuda_gl_capture: the real CudaGlCapture on a fake CUDA
# API (testutil/fake_cuda.h) and a GL 3.3 context; SKIP/77 without one, except on Linux CI where it must run.
# media_cuda_gl_hw: the same with the real driver; SKIP/77 without a GL 3.3 context, an NVIDIA GPU or a CUDA driver,
# also on CI. Run test_media_cuda_gl_hw by hand on a machine with an NVIDIA GPU.
if(BUILD_TESTING)
  framebeam_add_test(media_cuda tests/cuda_test.cpp LIBS framebeam_media)
  framebeam_add_test(media_cuda_gl_capture tests/cuda_gl_capture_test.cpp LIBS framebeam_media Qt6::Gui)
  set_tests_properties(media_cuda_gl_capture PROPERTIES SKIP_RETURN_CODE 77)
  framebeam_add_test(media_cuda_gl_hw tests/cuda_gl_hw_test.cpp LIBS framebeam_media Qt6::Gui)
  set_tests_properties(media_cuda_gl_hw PROPERTIES SKIP_RETURN_CODE 77)
endif()
