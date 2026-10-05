// Tiny fake libretro core (test only, no ROM): exposes SAVE_RAM. Configuration via environment:
//   FB_FAKE_SRAM_SIZE (default 8), FB_FAKE_SRAM_DELAY_FRAMES (no save memory for the first N frames),
//   FB_FAKE_SRAM_WRITE (byte written into SRAM[0] once memory is available, as a change by the "game").
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "third_party/libretro/libretro.h"

#if defined(_WIN32)
#define FB_EXPORT extern "C" __declspec(dllexport)
#else
#define FB_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
std::vector<uint8_t> g_sram;
int g_frames = 0;
int g_delay = 0;
int g_write = -1;
retro_video_refresh_t g_video = nullptr;
uint16_t g_pixels[16 * 16];
int envInt(const char* n, int def) {
  const char* v = std::getenv(n);
  return v ? std::atoi(v) : def;
}
bool memAvailable() { return g_frames >= g_delay; }
}  // namespace

FB_EXPORT unsigned retro_api_version() { return RETRO_API_VERSION; }
FB_EXPORT void retro_set_environment(retro_environment_t) {}
FB_EXPORT void retro_set_video_refresh(retro_video_refresh_t v) { g_video = v; }
FB_EXPORT void retro_set_audio_sample(retro_audio_sample_t) {}
FB_EXPORT void retro_set_audio_sample_batch(retro_audio_sample_batch_t) {}
FB_EXPORT void retro_set_input_poll(retro_input_poll_t) {}
FB_EXPORT void retro_set_input_state(retro_input_state_t) {}
FB_EXPORT void retro_init() {}
FB_EXPORT void retro_deinit() {}
FB_EXPORT void retro_get_system_info(retro_system_info* i) {
  std::memset(i, 0, sizeof(*i));
  i->library_name = "fake";
  i->library_version = "0";
  i->valid_extensions = "bin";
  i->need_fullpath = false;
}
FB_EXPORT void retro_get_system_av_info(retro_system_av_info* i) {
  std::memset(i, 0, sizeof(*i));
  i->geometry.base_width = i->geometry.max_width = 16;
  i->geometry.base_height = i->geometry.max_height = 16;
  i->geometry.aspect_ratio = 1.0f;
  i->timing.fps = 60.0;
  i->timing.sample_rate = 44100.0;
}
FB_EXPORT bool retro_load_game(const retro_game_info*) {
  g_sram.assign(static_cast<size_t>(envInt("FB_FAKE_SRAM_SIZE", 8)), 0);
  g_delay = envInt("FB_FAKE_SRAM_DELAY_FRAMES", 0);
  g_write = envInt("FB_FAKE_SRAM_WRITE", -1);
  g_frames = 0;
  return true;
}
FB_EXPORT void retro_unload_game() {}
FB_EXPORT void retro_run() {
  ++g_frames;
  if (g_write >= 0 && g_frames > g_delay && !g_sram.empty()) g_sram[0] = static_cast<uint8_t>(g_write);
  if (g_video) g_video(g_pixels, 16, 16, 32);
}
FB_EXPORT void retro_reset() {}
FB_EXPORT void* retro_get_memory_data(unsigned t) { return (t == RETRO_MEMORY_SAVE_RAM && memAvailable()) ? g_sram.data() : nullptr; }
FB_EXPORT size_t retro_get_memory_size(unsigned t) { return (t == RETRO_MEMORY_SAVE_RAM && memAvailable()) ? g_sram.size() : 0; }
