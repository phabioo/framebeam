#pragma once
// LibretroBackend: laedt einen libretro-Core per QLibrary (dlopen/LoadLibrary, kein Link).
//
// Einschraenkung: libretro-Cores haben globalen Zustand und ein C-API ohne Instanz-Handle.
// Pro Prozess darf daher nur EIN LibretroBackend einen Core geladen haben; loadCore() eines
// zweiten Backends schlaegt mit Fehlermeldung fehl, bis das erste unloadCore() aufgerufen hat.
//
// Hardware-Rendering wird abgelehnt (Cores nutzen den Software-Renderer). Nicht unterstuetzte
// Environment-Callbacks werden mit false beantwortet.

#include <QLibrary>
#include <QMap>
#include <QMutex>

#include <map>
#include <memory>
#include <string>

#include "emulator_backend.h"

namespace framebeam::emu {

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

  bool runFrame() override;
  void reset() override;

  QImage videoFrame() const override;
  quint64 frameCount() const override;
  QByteArray takeAudio() override;

  void setJoypadState(unsigned port, quint32 buttonMask) override;
  void setPointer(double x, double y, bool pressed) override;

  QList<CoreOption> coreOptions() const override;
  QList<CoreOptionCategory> coreOptionCategories() const override;
  bool setCoreOption(const QString& key, const QString& value) override;

  // Core hat per RETRO_ENVIRONMENT_SHUTDOWN um Beenden gebeten.
  bool shutdownRequested() const { return m_shutdownRequested; }

 private:
  struct Api;
  struct InputState {
    quint32 joypad[2] = {0, 0};
    double px = 0.0;
    double py = 0.0;
    bool pressed = false;
  };

  // libretro-Callbacks (C-Linkage-kompatibel, ohne Kontext -> statische Instanz).
  static bool environmentCb(unsigned cmd, void* data);
  static void videoRefreshCb(const void* data, unsigned width, unsigned height, size_t pitch);
  static void audioSampleCb(int16_t left, int16_t right);
  static size_t audioBatchCb(const int16_t* data, size_t frames);
  static void inputPollCb();
  static int16_t inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id);

  bool handleEnvironment(unsigned cmd, void* data);
  void handleVideo(const void* data, unsigned width, unsigned height, size_t pitch);
  void registerOptionsV2(const void* options);
  void registerOptionsV1(const void* definitions);
  void registerOptionsV0(const void* variables);
  void setOptionVisible(const QString& key, bool visible);
  QString effectiveValue(const CoreOption& o) const;  // m_optMutex gehalten

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
  QByteArray m_corePathUtf8;
  QByteArray m_gameData;  // lebt bis unloadGame (Core darf Zeiger halten)
  QByteArray m_gamePathUtf8;

  int m_pixelFormat = 0;  // RETRO_PIXEL_FORMAT_*
  QImage m_frame;
  quint64 m_frameCount = 0;
  QByteArray m_audio;

  mutable QMutex m_inputMutex;
  InputState m_input;   // von der UI geschrieben
  InputState m_polled;  // Kern-Thread, bei input_poll uebernommen

  mutable QMutex m_optMutex;
  QList<CoreOption> m_options;
  QList<CoreOptionCategory> m_categories;
  QMap<QString, QString> m_overrides;  // gewuenschte Werte (auch vor Registrierung)
  std::map<std::string, std::string> m_varCache;  // stabile Zeiger fuer GET_VARIABLE
  bool m_optionsDirty = false;
};

}  // namespace framebeam::emu
