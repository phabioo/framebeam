#pragma once
// EmulatorBackend: core-agnostische Schnittstelle zwischen FrameBeam Player und Emulator.
// Nach aussen gilt ein einheitliches Format: Video = QImage::Format_RGB32 (XRGB8888),
// Audio = interleaved Stereo int16 (L,R,L,R,... in nativer Byte-Reihenfolge).
//
// Threading: Alle Methoden ausser den mit "thread-sicher" markierten muessen vom selben Thread
// aufgerufen werden (im Player der Emulationsthread, siehe EmulationRunner).

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace framebeam::emu {

struct CoreInfo {
  QString name;
  QString version;
  QStringList extensions;  // normalisiert: klein, mit Punkt (".nds")
  bool needFullpath = false;
};

struct AvInfo {
  int width = 0;  // Basis-Geometrie des Cores; tatsaechliche Frame-Groesse steht im QImage
  int height = 0;
  double aspectRatio = 0.0;
  double fps = 0.0;
  double sampleRate = 0.0;  // Hz, vom Core vorgegeben
};

// Werte entsprechen RETRO_DEVICE_ID_JOYPAD_*; Bitmaske = 1u << Wert.
enum class JoypadButton : unsigned {
  B = 0, Y, Select, Start, Up, Down, Left, Right, A, X, L, R, L2, R2, L3, R3
};
constexpr quint32 buttonMask(JoypadButton b) { return 1u << static_cast<unsigned>(b); }

struct CoreOptionValue {
  QString value;
  QString label;  // leer = value anzeigen
};

struct CoreOptionCategory {
  QString key;
  QString description;
  QString info;
};

struct CoreOption {
  QString key;
  QString description;
  QString info;
  QString categoryKey;  // leer = ohne Kategorie
  QList<CoreOptionValue> values;
  QString defaultValue;
  QString currentValue;
  bool visible = true;
};

class EmulatorBackend {
 public:
  virtual ~EmulatorBackend() = default;

  // Core laden/entladen. Fehlertext (fuer Diagnostics) in *error.
  virtual bool loadCore(const QString& libraryPath, QString* error = nullptr) = 0;
  virtual void unloadCore() = 0;
  virtual bool isCoreLoaded() const = 0;
  virtual CoreInfo coreInfo() const = 0;  // gueltig nach loadCore

  // Verzeichnisse VOR loadCore setzen (System = BIOS/Firmware, Save = Spielstaende); Cores wie
  // melonDS DS lesen sie bereits in retro_set_environment.
  virtual void setSystemDirectory(const QString& path) = 0;
  virtual void setSaveDirectory(const QString& path) = 0;

  virtual bool loadGame(const QString& path, QString* error = nullptr) = 0;
  virtual void unloadGame() = 0;
  virtual bool isGameLoaded() const = 0;
  virtual AvInfo avInfo() const = 0;  // gueltig nach loadGame

  // Genau einen Frame emulieren; danach videoFrame()/takeAudio(). false = Fehler/Core-Shutdown.
  virtual bool runFrame() = 0;
  virtual void reset() = 0;

  // Letzter Video-Frame (XRGB8888). Implizit geteilt, billig zu kopieren.
  virtual QImage videoFrame() const = 0;
  virtual quint64 frameCount() const = 0;
  // Seit dem letzten Aufruf erzeugte Audio-Samples (interleaved Stereo int16).
  virtual QByteArray takeAudio() = 0;

  // thread-sicher: Eingabe-Zustand, wird beim naechsten runFrame uebernommen.
  virtual void setJoypadState(unsigned port, quint32 buttonMask) = 0;
  // Zeiger/Touch in normierten Koordinaten (0..1) relativ zum gesamten Video-Frame.
  virtual void setPointer(double x, double y, bool pressed) = 0;

  // Core Options: dynamisch vom Core gemeldet (Libretro Core Options v0/v1/v2).
  virtual QList<CoreOption> coreOptions() const = 0;                 // thread-sicher
  virtual QList<CoreOptionCategory> coreOptionCategories() const = 0;  // thread-sicher
  // thread-sicher. Vor loadGame gesetzte Werte werden gemerkt und beim Start angewandt.
  // false: Option bekannt, Wert ungueltig, oder Option nach der Registrierung unbekannt.
  virtual bool setCoreOption(const QString& key, const QString& value) = 0;
};

}  // namespace framebeam::emu
