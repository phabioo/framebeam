// Fake libretro core with OpenGL hardware rendering (test only, no ROM). It requests an OpenGL Core 3.3
// context in retro_load_game and draws a pattern that proves the orientation of the readback (GL origin is
// bottom-left): blue background, red 8x8 square at GL (0,0) = bottom-left, green 8x8 square at the
// top-right corner of the 64x48 frame. The FBO is larger (max 128x96) than the frame on purpose.
// Environment: FB_FAKE_HW_BOTTOM_LEFT (default 1), FB_FAKE_HW_CTX (default 3 = OPENGL_CORE, 1 = OPENGL,
// 6 = Vulkan, to test rejection), FB_FAKE_HW_MAJOR/MINOR (requested GL version, default 3.3), FB_FAKE_HW_LOG (file; events are appended as lines: accepted, rejected,
// reset <major>.<minor>, destroy, frame <fbo-id>).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "third_party/libretro/libretro.h"

#if defined(_WIN32)
#define FB_EXPORT extern "C" __declspec(dllexport)
#define FB_GL __stdcall
#else
#define FB_EXPORT extern "C" __attribute__((visibility("default")))
#define FB_GL
#endif

namespace {
constexpr unsigned kW = 64, kH = 48;

using GlBindFramebuffer = void(FB_GL*)(unsigned, unsigned);
using GlClearColor = void(FB_GL*)(float, float, float, float);
using GlClear = void(FB_GL*)(unsigned);
using GlEnable = void(FB_GL*)(unsigned);
using GlScissor = void(FB_GL*)(int, int, int, int);
using GlViewport = void(FB_GL*)(int, int, int, int);
using GlGetIntegerv = void(FB_GL*)(unsigned, int*);

void logLine(const char* fmt, int a = 0, int b = 0) {
  const char* path = std::getenv("FB_FAKE_HW_LOG");
  if (!path) return;
  if (std::FILE* f = std::fopen(path, "a")) {
    std::fprintf(f, fmt, a, b);
    std::fputc('\n', f);
    std::fclose(f);
  }
}

uintptr_t g_lastFbo = 0;

retro_environment_t g_env = nullptr;
retro_video_refresh_t g_video = nullptr;
retro_hw_render_callback g_hw{};
GlBindFramebuffer glBindFramebuffer_ = nullptr;
GlClearColor glClearColor_ = nullptr;
GlClear glClear_ = nullptr;
GlEnable glEnable_ = nullptr;
GlScissor glScissor_ = nullptr;
GlViewport glViewport_ = nullptr;

template <typename F>
void load(F& fn, const char* name) {
  fn = reinterpret_cast<F>(g_hw.get_proc_address(name));
}

int envInt(const char* n, int def) {
  const char* v = std::getenv(n);
  return v ? std::atoi(v) : def;
}

void RETRO_CALLCONV contextReset() {
  load(glBindFramebuffer_, "glBindFramebuffer");
  load(glClearColor_, "glClearColor");
  load(glClear_, "glClear");
  load(glEnable_, "glEnable");
  load(glScissor_, "glScissor");
  load(glViewport_, "glViewport");
  GlGetIntegerv getIntegerv = nullptr;
  load(getIntegerv, "glGetIntegerv");
  int major = 0, minor = 0;
  if (getIntegerv) {
    getIntegerv(0x821B, &major);  // GL_MAJOR_VERSION
    getIntegerv(0x821C, &minor);  // GL_MINOR_VERSION
  }
  logLine("reset %d.%d", major, minor);
}

void RETRO_CALLCONV contextDestroy() { logLine("destroy"); }
}  // namespace

FB_EXPORT unsigned retro_api_version() { return RETRO_API_VERSION; }
FB_EXPORT void retro_set_environment(retro_environment_t e) { g_env = e; }
FB_EXPORT void retro_set_video_refresh(retro_video_refresh_t v) { g_video = v; }
FB_EXPORT void retro_set_audio_sample(retro_audio_sample_t) {}
FB_EXPORT void retro_set_audio_sample_batch(retro_audio_sample_batch_t) {}
FB_EXPORT void retro_set_input_poll(retro_input_poll_t) {}
FB_EXPORT void retro_set_input_state(retro_input_state_t) {}
FB_EXPORT void retro_init() {}
FB_EXPORT void retro_deinit() {}
FB_EXPORT void retro_get_system_info(retro_system_info* i) {
  std::memset(i, 0, sizeof(*i));
  i->library_name = "fake-hw";
  i->library_version = "0";
  i->valid_extensions = "bin";
  i->need_fullpath = false;
}
FB_EXPORT void retro_get_system_av_info(retro_system_av_info* i) {
  std::memset(i, 0, sizeof(*i));
  i->geometry.base_width = kW;
  i->geometry.base_height = kH;
  i->geometry.max_width = 2 * kW;
  i->geometry.max_height = 2 * kH;
  i->geometry.aspect_ratio = static_cast<float>(kW) / kH;
  i->timing.fps = 60.0;
  i->timing.sample_rate = 44100.0;
}
FB_EXPORT bool retro_load_game(const retro_game_info*) {
  int fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  g_env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
  std::memset(&g_hw, 0, sizeof g_hw);
  g_hw.context_type = static_cast<retro_hw_context_type>(envInt("FB_FAKE_HW_CTX", RETRO_HW_CONTEXT_OPENGL_CORE));
  g_hw.version_major = static_cast<unsigned>(envInt("FB_FAKE_HW_MAJOR", 3));
  g_hw.version_minor = static_cast<unsigned>(envInt("FB_FAKE_HW_MINOR", 3));
  g_hw.context_reset = contextReset;
  g_hw.context_destroy = contextDestroy;
  g_hw.bottom_left_origin = envInt("FB_FAKE_HW_BOTTOM_LEFT", 1) != 0;
  if (!g_env(RETRO_ENVIRONMENT_SET_HW_RENDER, &g_hw)) {
    logLine("rejected");
    return false;
  }
  logLine("accepted");
  return true;
}
FB_EXPORT void retro_unload_game() {}
FB_EXPORT void retro_run() {
  if (!glBindFramebuffer_) return;
  g_lastFbo = g_hw.get_current_framebuffer();
  if (g_lastFbo == 0) logLine("frame without fbo");
  glBindFramebuffer_(0x8D40, static_cast<unsigned>(g_lastFbo));  // GL_FRAMEBUFFER
  glViewport_(0, 0, kW, kH);
  glEnable_(0x0C11);  // GL_SCISSOR_TEST
  glScissor_(0, 0, kW, kH);
  glClearColor_(0.f, 0.f, 1.f, 1.f);
  glClear_(0x4000);  // GL_COLOR_BUFFER_BIT
  glScissor_(0, 0, 8, 8);
  glClearColor_(1.f, 0.f, 0.f, 1.f);
  glClear_(0x4000);
  glScissor_(static_cast<int>(kW) - 8, static_cast<int>(kH) - 8, 8, 8);
  glClearColor_(0.f, 1.f, 0.f, 1.f);
  glClear_(0x4000);
  g_video(RETRO_HW_FRAME_BUFFER_VALID, kW, kH, 0);
}
FB_EXPORT void retro_reset() {}
FB_EXPORT void* retro_get_memory_data(unsigned) { return nullptr; }
FB_EXPORT size_t retro_get_memory_size(unsigned) { return 0; }
