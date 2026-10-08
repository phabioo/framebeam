#pragma once
// LibretroBackend: loads a libretro core via QLibrary (dlopen/LoadLibrary, no linking).
//
// Limitation: libretro cores have global state and a C API without an instance handle.
// Therefore only ONE LibretroBackend per process may have a core loaded; loadCore() of a
// second backend fails with an error message until the first has called unloadCore().
//
// Hardware rendering (ADR 0013): SET_HW_RENDER with OpenGL / OpenGL Core is served by an offscreen context
// owned by the emulation thread (hw_render.h); every hardware frame is read back into the XRGB8888 frame.
// Without a QGuiApplication, with FRAMEBEAM_DISABLE_HW_RENDER=1 or when the context fails, SET_HW_RENDER
// returns false (the core falls back to software). Other APIs are rejected. Unsupported environment
// callbacks are answered with false.

#include <QLibrary>
#include <QMap>
#include <QMutex>

#include <atomic>
#include <map>
#include <memory>
#include <string>

#include "emulator_backend.h"

namespace framebeam::emu {

class HwRenderContext;

class LibretroBackend final : public EmulatorBackend {
 public:
  LibretroBackend();
  ~LibretroBackend() override;
  Q_DISABLE_COPY(LibretroBackend)

  bool loadCore(const QString& libraryPath, QString* error = nullptr) override;
  void unloadCore() override;
  bool isCoreLoaded() const override;
  CoreInfo coreInfo() const override;

  void setSystemDirectory(const QString& path) override;
  void setSaveDirectory(const QString& path) override;

  bool loadGame(const QString& path, QString* error = nullptr) override;
  void unloadGame() override;
  bool isGameLoaded() const override;
  AvInfo avInfo() const override;

  void setVideoWanted(bool wanted) override { m_videoWanted.store(wanted); }
  bool runFrame() override;
  void reset() override;
  // Battery save (RETRO_MEMORY_SAVE_RAM) <-> <save dir>/<game basename>.sav: loaded after the game loads,
  // written when changed (about every 3 s while running, on pause and before unloading), atomically.
  void flushSave() override;
  // GUI thread, before the emulation thread starts: creates the offscreen surface for hardware rendering.
  void prepareForStart() override;

  QImage videoFrame() const override;
  quint64 frameCount() const override;
  QByteArray takeAudio() override;
  double lastReadbackMs() const override;
  // Diagnostics/tests: number of hardware frames read back from the GPU so far.
  quint64 hwReadbackCount() const { return m_hwReadbacks.load(); }
  RenderInfo renderInfo() const override;

  bool supportsFastForward() const override { return !m_ffInhibit.load(); }
  void setFastForwarding(bool on, double ratio) override {
    m_ffRatio.store(ratio);
    m_fastForwarding.store(on);
  }

  void setJoypadState(unsigned port, quint32 buttonMask) override;
  void setPointer(double x, double y, bool pressed) override;

  QList<CoreOption> coreOptions() const override;
  QList<CoreOptionCategory> coreOptionCategories() const override;
  bool setCoreOption(const QString& key, const QString& value) override;

  // The core requested shutdown via RETRO_ENVIRONMENT_SHUTDOWN.
  bool shutdownRequested() const { return m_shutdownRequested; }

 private:
  struct Api;
  struct InputState {
    quint32 joypad[2] = {0, 0};
    double px = 0.0;
    double py = 0.0;
    bool pressed = false;
  };

  // libretro callbacks (C-linkage compatible, no context -> static instance).
  static bool environmentCb(unsigned cmd, void* data);
  static void videoRefreshCb(const void* data, unsigned width, unsigned height, size_t pitch);
  static void audioSampleCb(int16_t left, int16_t right);
  static size_t audioBatchCb(const int16_t* data, size_t frames);
  static void inputPollCb();
  static int16_t inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id);

  bool tryLoadSave();
  static QString backupStamp();
  bool handleEnvironment(unsigned cmd, void* data);
  bool setupHwRender(void* retroHwRenderCallback);
  bool finishHwSetup(unsigned maxWidth, unsigned maxHeight, QString* error);  // after retro_load_game
  void teardownHw();

  void handleVideo(const void* data, unsigned width, unsigned height, size_t pitch);
  void registerOptionsV2(const void* options);
  void registerOptionsV1(const void* definitions);
  void registerOptionsV0(const void* variables);
  void setOptionVisible(const QString& key, bool visible);
  QString effectiveValue(const CoreOption& o) const;  // m_optMutex held

  static LibretroBackend* s_active;

  std::unique_ptr<Api> m_api;
  QLibrary m_lib;
  QString m_corePath;
  CoreInfo m_info;
  AvInfo m_av;
  bool m_coreLoaded = false;
  bool m_gameLoaded = false;
  bool m_shutdownRequested = false;

  QByteArray m_systemDir;
  QByteArray m_saveDir;
  QString m_saveFilePath;
  QByteArray m_sramSnapshot;  // content last loaded/written
  unsigned m_framesSinceFlush = 0;
  bool m_savePendingLoad = false;  // save memory not yet exposed / file not yet applied
  bool m_saveBlocked = false;      // existing file unreadable: never written
  QByteArray m_corePathUtf8;
  QByteArray m_gameData;  // lives until unloadGame (the core may hold pointers)
  QByteArray m_gamePathUtf8;

  std::unique_ptr<HwRenderContext> m_hw;
  // Core's hardware callbacks (copied from retro_hw_render_callback).
  void (*m_hwContextReset)() = nullptr;
  void (*m_hwContextDestroy)() = nullptr;
  bool m_hwActive = false;      // SET_HW_RENDER accepted, context exists
  bool m_hwResetDone = false;   // context_reset called (context_destroy owed)
  bool m_hwBottomLeft = true;
  mutable QMutex m_renderMutex;
  RenderInfo m_render;                     // diagnostics, written on load/setup, read from the UI thread
  std::atomic<qint64> m_lastReadbackNs{0};
  std::atomic<bool> m_videoWanted{true};
  std::atomic<quint64> m_hwReadbacks{0};
  std::atomic<bool> m_fastForwarding{false};
  std::atomic<bool> m_ffInhibit{false};  // the core forbids toggling fast-forward
  std::atomic<double> m_ffRatio{1.0};    // speed multiple while fast-forwarding (chosen by the user)

  int m_pixelFormat = 0;  // RETRO_PIXEL_FORMAT_*
  QImage m_frame;
  quint64 m_frameCount = 0;
  QByteArray m_audio;

  mutable QMutex m_inputMutex;
  InputState m_input;   // written by the UI
  InputState m_polled;  // core thread, taken over at input_poll

  mutable QMutex m_optMutex;
  QList<CoreOption> m_options;
  QList<CoreOptionCategory> m_categories;
  QMap<QString, QString> m_overrides;  // requested values (also before registration)
  std::map<std::string, std::string> m_varCache;  // stable pointers for GET_VARIABLE
  bool m_optionsDirty = false;
};

}  // namespace framebeam::emu
