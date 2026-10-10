// Fake libretro core for the no-game probe tests (no ROM). Built twice from this file:
//   FB_DECLARE_NO_GAME=1: declares SET_SUPPORT_NO_GAME and registers its option (SET_VARIABLES) only in retro_load_game.
//   FB_DECLARE_NO_GAME=0: does not declare it; retro_load_game dereferences info like the Azahar core (crashes on NULL),
//                         the option is registered in retro_init.
#include <cstring>

#include "third_party/libretro/libretro.h"

#if defined(_WIN32)
#define FB_EXPORT extern "C" __declspec(dllexport)
#else
#define FB_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#ifndef FB_DECLARE_NO_GAME
#define FB_DECLARE_NO_GAME 0
#endif

namespace {
retro_environment_t g_env = nullptr;
const char* g_path = nullptr;
void registerOption() {
  static const retro_variable vars[] = {{"fake_opt", "Fake option; a|b"}, {nullptr, nullptr}};
  if (g_env) g_env(RETRO_ENVIRONMENT_SET_VARIABLES, const_cast<retro_variable*>(vars));
}
}  // namespace

FB_EXPORT unsigned retro_api_version() { return RETRO_API_VERSION; }
FB_EXPORT void retro_set_environment(retro_environment_t e) {
  g_env = e;
#if FB_DECLARE_NO_GAME
  bool yes = true;
  e(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &yes);
#endif
}
FB_EXPORT void retro_set_video_refresh(retro_video_refresh_t) {}
FB_EXPORT void retro_set_audio_sample(retro_audio_sample_t) {}
FB_EXPORT void retro_set_audio_sample_batch(retro_audio_sample_batch_t) {}
FB_EXPORT void retro_set_input_poll(retro_input_poll_t) {}
FB_EXPORT void retro_set_input_state(retro_input_state_t) {}
FB_EXPORT void retro_init() {
#if !FB_DECLARE_NO_GAME
  registerOption();
#endif
}
FB_EXPORT void retro_deinit() {}
FB_EXPORT void retro_get_system_info(retro_system_info* i) {
  std::memset(i, 0, sizeof(*i));
  i->library_name = "fake_nogame";
  i->library_version = "1";
  i->valid_extensions = "bin";
}
FB_EXPORT void retro_get_system_av_info(retro_system_av_info* i) {
  std::memset(i, 0, sizeof(*i));
  i->geometry.base_width = i->geometry.max_width = 16;
  i->geometry.base_height = i->geometry.max_height = 16;
  i->geometry.aspect_ratio = 1.0f;
  i->timing.fps = 60.0;
  i->timing.sample_rate = 44100.0;
}
FB_EXPORT bool retro_load_game(const retro_game_info* info) {
#if FB_DECLARE_NO_GAME
  registerOption();
  return true;
#else
  g_path = info->path;  // NULL info: access violation, as in the Azahar core
  return g_path != nullptr;
#endif
}
FB_EXPORT void retro_unload_game() {}
FB_EXPORT void retro_run() {}
FB_EXPORT void retro_reset() {}
FB_EXPORT void* retro_get_memory_data(unsigned) { return nullptr; }
FB_EXPORT size_t retro_get_memory_size(unsigned) { return 0; }
