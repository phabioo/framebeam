#include "libretro_backend.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <QHash>
#include <mutex>
#include <cstdio>
#include <cstring>

#include "hw_render.h"
#include "third_party/libretro/libretro.h"

namespace framebeam::emu {

namespace {
Q_LOGGING_CATEGORY(lcCore, "framebeam.emulation.core")

// The core repeats the same lines for every frame (OSD texts via SET_MESSAGE, debug lines): only the first
// occurrence of each distinct message is reported, then every 1000th (with the count). false = suppress.
bool dedupeLog(QString& msg) {
  static std::mutex m;
  static QHash<QString, int> seen;
  std::lock_guard<std::mutex> lock(m);
  if (seen.size() > 256) seen.clear();
  const int n = seen[msg]++;
  if (n == 0) return true;
  if (n % 1000 != 0) return false;
  msg += QStringLiteral(" (repeated %1 times)").arg(n);
  return true;
}

void RETRO_CALLCONV coreLog(enum retro_log_level level, const char* fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt ? fmt : "", ap);
  va_end(ap);
  QString msg = QString::fromUtf8(buf).trimmed();
  if (msg.isEmpty()) return;
  if (!dedupeLog(msg)) return;
  switch (level) {
    case RETRO_LOG_ERROR: qCWarning(lcCore).noquote() << "[error]" << msg; break;
    case RETRO_LOG_WARN: qCWarning(lcCore).noquote() << msg; break;
    case RETRO_LOG_INFO: qCInfo(lcCore).noquote() << msg; break;
    default: qCDebug(lcCore).noquote() << msg; break;
  }
}

HwRenderContext* s_hwCtx = nullptr;  // context of the active backend, for the C callbacks below

uintptr_t RETRO_CALLCONV hwGetFramebuffer() { return s_hwCtx ? s_hwCtx->framebuffer() : 0; }
retro_proc_address_t RETRO_CALLCONV hwGetProcAddress(const char* sym) {
  return s_hwCtx ? reinterpret_cast<retro_proc_address_t>(s_hwCtx->procAddress(sym)) : nullptr;
}

QString fromC(const char* s) { return s ? QString::fromUtf8(s) : QString(); }

CoreOption makeOption(const char* key, const QString& desc, const QString& info, const QString& cat,
                      const retro_core_option_value* vals, const char* def) {
  CoreOption o;
  o.key = fromC(key);
  o.description = desc;
  o.info = info;
  o.categoryKey = cat;
  for (int i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && vals[i].value; ++i)
    o.values.append({fromC(vals[i].value), fromC(vals[i].label)});
  o.defaultValue = fromC(def);
  if (o.defaultValue.isEmpty() && !o.values.isEmpty()) o.defaultValue = o.values.first().value;
  o.currentValue = o.defaultValue;
  return o;
}

void applyOverrides(QList<CoreOption>& options, const QMap<QString, QString>& overrides) {
  for (CoreOption& opt : options) {
    auto it = overrides.constFind(opt.key);
    if (it == overrides.constEnd()) continue;
    for (const auto& v : std::as_const(opt.values))
      if (v.value == it.value()) opt.currentValue = it.value();
  }
}

}  // namespace

struct LibretroBackend::Api {
  void(RETRO_CALLCONV* set_environment)(retro_environment_t) = nullptr;
  void(RETRO_CALLCONV* set_video_refresh)(retro_video_refresh_t) = nullptr;
  void(RETRO_CALLCONV* set_audio_sample)(retro_audio_sample_t) = nullptr;
  void(RETRO_CALLCONV* set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
  void(RETRO_CALLCONV* set_input_poll)(retro_input_poll_t) = nullptr;
  void(RETRO_CALLCONV* set_input_state)(retro_input_state_t) = nullptr;
  void(RETRO_CALLCONV* init)() = nullptr;
  void(RETRO_CALLCONV* deinit)() = nullptr;
  unsigned(RETRO_CALLCONV* api_version)() = nullptr;
  void(RETRO_CALLCONV* get_system_info)(retro_system_info*) = nullptr;
  void(RETRO_CALLCONV* get_system_av_info)(retro_system_av_info*) = nullptr;
  bool(RETRO_CALLCONV* load_game)(const retro_game_info*) = nullptr;
  void(RETRO_CALLCONV* unload_game)() = nullptr;
  void(RETRO_CALLCONV* run)() = nullptr;
  void(RETRO_CALLCONV* reset)() = nullptr;
  void*(RETRO_CALLCONV* get_memory_data)(unsigned) = nullptr;  // optional
  size_t(RETRO_CALLCONV* get_memory_size)(unsigned) = nullptr;  // optional
};

LibretroBackend* LibretroBackend::s_active = nullptr;

LibretroBackend::LibretroBackend() : m_api(std::make_unique<Api>()) {}

LibretroBackend::~LibretroBackend() { unloadCore(); }

bool LibretroBackend::loadCore(const QString& libraryPath, QString* error) {
  auto fail = [&](const QString& msg) {
    if (error) *error = msg;
    return false;
  };
  if (m_coreLoaded) return fail(QStringLiteral("Core already loaded"));
  if (s_active) return fail(QStringLiteral("A libretro core is already loaded in this process"));

  m_lib.setFileName(libraryPath);
  if (!m_lib.load()) return fail(m_lib.errorString());

  bool ok = true;
  auto sym = [&](auto& fn, const char* name) {
    fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(m_lib.resolve(name));
    if (!fn) ok = false;
  };
  Api& a = *m_api;
  sym(a.set_environment, "retro_set_environment");
  sym(a.set_video_refresh, "retro_set_video_refresh");
  sym(a.set_audio_sample, "retro_set_audio_sample");
  sym(a.set_audio_sample_batch, "retro_set_audio_sample_batch");
  sym(a.set_input_poll, "retro_set_input_poll");
  sym(a.set_input_state, "retro_set_input_state");
  sym(a.init, "retro_init");
  sym(a.deinit, "retro_deinit");
  sym(a.api_version, "retro_api_version");
  sym(a.get_system_info, "retro_get_system_info");
  sym(a.get_system_av_info, "retro_get_system_av_info");
  sym(a.load_game, "retro_load_game");
  sym(a.unload_game, "retro_unload_game");
  sym(a.run, "retro_run");
  sym(a.reset, "retro_reset");
  a.get_memory_data = reinterpret_cast<void*(RETRO_CALLCONV*)(unsigned)>(m_lib.resolve("retro_get_memory_data"));
  a.get_memory_size = reinterpret_cast<size_t(RETRO_CALLCONV*)(unsigned)>(m_lib.resolve("retro_get_memory_size"));
  if (!ok) {
    m_lib.unload();
    return fail(QStringLiteral("Not a valid libretro library (symbols missing)"));
  }
  const unsigned apiVersion = a.api_version();
  if (apiVersion != RETRO_API_VERSION) {
    m_lib.unload();
    return fail(QStringLiteral("libretro API version %1 not supported").arg(apiVersion));
  }

  s_active = this;
  m_corePath = libraryPath;
  m_corePathUtf8 = QDir::toNativeSeparators(libraryPath).toUtf8();
  m_shutdownRequested = false;
  m_pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
  m_frame = QImage();
  m_frameCount = 0;
  m_audio.clear();
  m_lastReadbackNs = 0;
  {
    QMutexLocker l(&m_renderMutex);
    m_render = RenderInfo{};
  }
  {
    QMutexLocker l(&m_optMutex);
    m_options.clear();
    m_categories.clear();
  }

  a.set_environment(&LibretroBackend::environmentCb);
  a.set_video_refresh(&LibretroBackend::videoRefreshCb);
  a.set_audio_sample(&LibretroBackend::audioSampleCb);
  a.set_audio_sample_batch(&LibretroBackend::audioBatchCb);
  a.set_input_poll(&LibretroBackend::inputPollCb);
  a.set_input_state(&LibretroBackend::inputStateCb);
  a.init();

  retro_system_info si{};
  a.get_system_info(&si);
  m_info = CoreInfo{};
  m_info.name = fromC(si.library_name);
  m_info.version = fromC(si.library_version);
  m_info.needFullpath = si.need_fullpath;
  for (const QString& e : fromC(si.valid_extensions).split(QLatin1Char('|'), Qt::SkipEmptyParts))
    m_info.extensions.append(QLatin1Char('.') + e.toLower());

  m_coreLoaded = true;
  return true;
}

void LibretroBackend::unloadCore() {
  if (!m_coreLoaded) return;
  unloadGame();
  m_api->deinit();
  teardownHw();
  m_lib.unload();
  m_coreLoaded = false;
  s_active = nullptr;
  m_hw.reset();
}

bool LibretroBackend::isCoreLoaded() const { return m_coreLoaded; }
CoreInfo LibretroBackend::coreInfo() const { return m_info; }

void LibretroBackend::setSystemDirectory(const QString& path) {
  m_systemDir = QDir::toNativeSeparators(path).toUtf8();
}
void LibretroBackend::setSaveDirectory(const QString& path) {
  m_saveDir = QDir::toNativeSeparators(path).toUtf8();
}

bool LibretroBackend::loadGame(const QString& path, QString* error) {
  auto fail = [&](const QString& msg) {
    if (error) *error = msg;
    return false;
  };
  if (!m_coreLoaded) return fail(QStringLiteral("No core loaded"));
  if (m_gameLoaded) unloadGame();

  // Empty path: no-game mode (retro_load_game(NULL)); only used to let a core register its options.
  const bool noGame = path.isEmpty();
  QFileInfo fi(path);
  if (!noGame && !fi.isFile()) return fail(QStringLiteral("Game file not found"));
  m_gamePathUtf8 = noGame ? QByteArray() : QDir::toNativeSeparators(fi.absoluteFilePath()).toUtf8();
  m_gameData.clear();

  retro_game_info gi{};
  gi.path = m_gamePathUtf8.constData();
  if (noGame) {
    m_shutdownRequested = false;
    m_frame = QImage();
    m_frameCount = 0;
    m_audio.clear();
    if (!m_api->load_game(nullptr)) {
      teardownHw();
      return fail(QStringLiteral("Core could not start without a game"));
    }
    if (m_hwActive) {
      retro_system_av_info hwAv{};
      m_api->get_system_av_info(&hwAv);
      QString hwErr;
      if (!finishHwSetup(hwAv.geometry.max_width, hwAv.geometry.max_height, &hwErr)) {
        m_api->unload_game();
        teardownHw();
        return fail(hwErr);
      }
    }
    m_gameLoaded = true;
    m_saveFilePath.clear();
    return true;
  }
  if (!m_info.needFullpath) {
    QFile f(fi.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) return fail(f.errorString());
    m_gameData = f.readAll();
    gi.data = m_gameData.constData();
    gi.size = static_cast<size_t>(m_gameData.size());
  }

  m_shutdownRequested = false;
  m_frame = QImage();
  m_frameCount = 0;
  m_audio.clear();
  if (!m_api->load_game(&gi)) {
    teardownHw();
    return fail(QStringLiteral("Core could not load the game"));
  }

  retro_system_av_info av{};
  m_api->get_system_av_info(&av);
  if (m_hwActive) {
    QString hwErr;
    if (!finishHwSetup(av.geometry.max_width, av.geometry.max_height, &hwErr)) {
      m_api->unload_game();
      teardownHw();
      return fail(hwErr);
    }
  }
  m_av.width = static_cast<int>(av.geometry.base_width);
  m_av.height = static_cast<int>(av.geometry.base_height);
  m_av.aspectRatio = static_cast<double>(av.geometry.aspect_ratio);
  m_av.fps = av.timing.fps;
  m_av.sampleRate = av.timing.sample_rate;
  m_gameLoaded = true;

  // Battery save: the core exposes it as memory, the frontend persists it (like RetroArch's .srm).
  m_saveFilePath.clear();
  m_sramSnapshot.clear();
  m_framesSinceFlush = 0;
  m_savePendingLoad = false;
  m_saveBlocked = false;
  if (!m_saveDir.isEmpty() && m_api->get_memory_data && m_api->get_memory_size) {
    m_saveFilePath = QDir(QString::fromUtf8(m_saveDir)).filePath(fi.completeBaseName() + QStringLiteral(".sav"));
    m_savePendingLoad = true;  // applied as soon as the core reports save memory (possibly only after a few frames)
    tryLoadSave();
  }
  return true;
}

// Loads the save file into SAVE_RAM once the core exposes it. Never overwrites a file that could not be
// read or whose size differs: such a file is backed up first (never deleted) and left alone.
bool LibretroBackend::tryLoadSave() {
  if (!m_savePendingLoad) return true;
  void* mem = m_api->get_memory_data(RETRO_MEMORY_SAVE_RAM);
  const size_t size = m_api->get_memory_size(RETRO_MEMORY_SAVE_RAM);
  if (mem == nullptr || size == 0) return false;
  m_savePendingLoad = false;
  const QFileInfo fi(m_saveFilePath);
  if (fi.exists()) {
    QFile sf(m_saveFilePath);
    if (!sf.open(QIODevice::ReadOnly)) {
      qCWarning(lcCore) << "Save file exists but cannot be read; it will not be overwritten:" << m_saveFilePath;
      QFile::copy(m_saveFilePath, m_saveFilePath + QStringLiteral(".unreadable-") + backupStamp() + QStringLiteral(".bak"));
      m_saveBlocked = true;
    } else {
      const QByteArray data = sf.readAll();
      if (static_cast<size_t>(data.size()) != size) {
        qCWarning(lcCore) << "Save file size" << data.size() << "differs from the core's save memory" << size
                          << "; keeping a backup before the first write";
        QFile::copy(m_saveFilePath, m_saveFilePath + QStringLiteral(".size-mismatch-") + backupStamp() + QStringLiteral(".bak"));
      }
      std::memcpy(mem, data.constData(), std::min(static_cast<size_t>(data.size()), size));
    }
  }
  m_sramSnapshot = QByteArray(static_cast<const char*>(mem), static_cast<qsizetype>(size));
  return true;
}

QString LibretroBackend::backupStamp() { return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")); }

void LibretroBackend::flushSave() {
  if (!m_gameLoaded || m_saveFilePath.isEmpty() || !m_api->get_memory_data || !m_api->get_memory_size) return;
  if (m_savePendingLoad && !tryLoadSave()) return;  // never flush before an existing file was applied
  if (m_saveBlocked) return;
  const void* mem = m_api->get_memory_data(RETRO_MEMORY_SAVE_RAM);
  const size_t size = m_api->get_memory_size(RETRO_MEMORY_SAVE_RAM);
  if (mem == nullptr || size == 0) return;
  const QByteArray now(static_cast<const char*>(mem), static_cast<qsizetype>(size));
  if (now == m_sramSnapshot) return;
  QSaveFile f(m_saveFilePath);
  if (f.open(QIODevice::WriteOnly) && f.write(now) == now.size() && f.commit()) {
    m_sramSnapshot = now;
  }
}

void LibretroBackend::unloadGame() {
  if (!m_gameLoaded) return;
  flushSave();
  if (m_hwActive) {
    m_hw->makeCurrent();
    if (m_hwResetDone && m_hwContextDestroy) m_hwContextDestroy();
    m_hwResetDone = false;
  }
  m_api->unload_game();
  teardownHw();
  m_gameLoaded = false;
  m_gameData.clear();
}

bool LibretroBackend::isGameLoaded() const { return m_gameLoaded; }
AvInfo LibretroBackend::avInfo() const { return m_av; }

bool LibretroBackend::runFrame() {
  if (!m_gameLoaded || m_shutdownRequested) return false;
  if (m_hwActive && !m_hw->makeCurrent()) return false;
  m_api->run();
  if (m_savePendingLoad) tryLoadSave();  // as soon as the core exposes save memory, before any flush
  if (++m_framesSinceFlush >= 180) {
    m_framesSinceFlush = 0;
    flushSave();
  }
  return !m_shutdownRequested;
}

void LibretroBackend::reset() {
  if (m_gameLoaded) m_api->reset();
}

QImage LibretroBackend::videoFrame() const { return m_frame; }
quint64 LibretroBackend::frameCount() const { return m_frameCount; }

double LibretroBackend::lastReadbackMs() const { return static_cast<double>(m_lastReadbackNs.load()) / 1e6; }

RenderInfo LibretroBackend::renderInfo() const {
  QMutexLocker l(&m_renderMutex);
  return m_render;
}

QByteArray LibretroBackend::takeAudio() {
  QByteArray out;
  out.swap(m_audio);
  return out;
}

void LibretroBackend::setJoypadState(unsigned port, quint32 mask) {
  if (port > 1) return;
  QMutexLocker l(&m_inputMutex);
  m_input.joypad[port] = mask;
}

void LibretroBackend::setPointer(double x, double y, bool pressed) {
  QMutexLocker l(&m_inputMutex);
  m_input.px = std::clamp(x, 0.0, 1.0);
  m_input.py = std::clamp(y, 0.0, 1.0);
  m_input.pressed = pressed;
}

// ---------------------------------------------------------------- Hardware rendering

void LibretroBackend::prepareForStart() {
  if (!HwRenderContext::allowed()) return;
  if (!m_hw) m_hw = std::make_unique<HwRenderContext>();
  m_hw->prepareSurface();
}

bool LibretroBackend::setupHwRender(void* data) {
  auto* cb = static_cast<retro_hw_render_callback*>(data);
  if (!cb || m_hwActive) return false;
  // Diagnostics: a request for hardware that ends in software gets a reason (shown as "OpenGL requested · fell back").
  auto fallback = [this](const char* reason) {
    QMutexLocker l(&m_renderMutex);
    m_render.hwRequested = true;
    m_render.hwActive = false;
    m_render.fallbackReason = QString::fromLatin1(reason);
  };
  if (cb->context_type != RETRO_HW_CONTEXT_OPENGL && cb->context_type != RETRO_HW_CONTEXT_OPENGL_CORE) {
    if (cb->context_type == RETRO_HW_CONTEXT_NONE) return false;  // melonDS DS announces "no hardware" for software mode
    qCInfo(lcCore) << "Hardware render API" << static_cast<int>(cb->context_type) << "not supported; core falls back to software";
    fallback(kFallbackUnsupported);
    return false;
  }
  if (!HwRenderContext::allowed()) {
    qCInfo(lcCore) << "Hardware rendering disabled (no GUI application or FRAMEBEAM_DISABLE_HW_RENDER=1); core falls back to software";
    fallback(qEnvironmentVariable("FRAMEBEAM_DISABLE_HW_RENDER") == QLatin1String("1") ? kFallbackDisabled : kFallbackNoContext);
    return false;
  }
  if (!m_hw) m_hw = std::make_unique<HwRenderContext>();
  QString err;
  const bool core = cb->context_type == RETRO_HW_CONTEXT_OPENGL_CORE;
  if (!m_hw->createContext(core, cb->version_major, cb->version_minor, cb->depth, cb->stencil, &err)) {
    qCWarning(lcCore).noquote() << "Hardware rendering unavailable:" << err << "; core falls back to software";
    fallback(kFallbackNoContext);
    return false;
  }
  s_hwCtx = m_hw.get();
  cb->get_current_framebuffer = &hwGetFramebuffer;
  cb->get_proc_address = &hwGetProcAddress;
  m_hwContextReset = cb->context_reset;
  m_hwContextDestroy = cb->context_destroy;
  m_hwBottomLeft = cb->bottom_left_origin;
  m_hwActive = true;
  m_hwResetDone = false;
  {
    QMutexLocker l(&m_renderMutex);
    m_render.hwRequested = true;
    m_render.hwActive = true;
    m_render.fallbackReason.clear();
    m_render.api = HwRenderContext::describeApi(m_hw->glVersion(), m_hw->isCoreProfile());
    m_render.gpu = HwRenderContext::describeGpu(m_hw->glRenderer(), m_hw->glVersion());
  }
  qCInfo(lcCore).noquote() << "Hardware rendering enabled (OpenGL" << (core ? "core" : "compat") << ")," << m_hw->glInfo();
  return true;
}

// After retro_load_game: size the FBO from the core's maximum geometry, then context_reset.
bool LibretroBackend::finishHwSetup(unsigned maxWidth, unsigned maxHeight, QString* error) {
  if (!m_hw->makeCurrent() ||
      !m_hw->ensureSize(static_cast<int>(maxWidth), static_cast<int>(maxHeight))) {
    if (error) *error = QStringLiteral("Hardware render framebuffer could not be created");
    QMutexLocker l(&m_renderMutex);
    m_render.hwActive = false;
    m_render.fallbackReason = QString::fromLatin1(kFallbackNoFramebuffer);
    return false;
  }
  if (m_hwContextReset) m_hwContextReset();
  m_hwResetDone = true;
  return true;
}

void LibretroBackend::teardownHw() {
  if (m_hw && m_hw->hasContext()) {
    m_hw->makeCurrent();
    if (m_hwResetDone && m_hwContextDestroy) m_hwContextDestroy();
    m_hw->destroyContext();
  }
  if (s_hwCtx == m_hw.get()) s_hwCtx = nullptr;
  m_hwActive = false;
  m_hwResetDone = false;
  m_hwContextReset = m_hwContextDestroy = nullptr;
}

// ---------------------------------------------------------------- Core Options

QList<CoreOption> LibretroBackend::coreOptions() const {
  QMutexLocker l(&m_optMutex);
  return m_options;
}

QList<CoreOptionCategory> LibretroBackend::coreOptionCategories() const {
  QMutexLocker l(&m_optMutex);
  return m_categories;
}

bool LibretroBackend::setCoreOption(const QString& key, const QString& value) {
  QMutexLocker l(&m_optMutex);
  for (CoreOption& o : m_options) {
    if (o.key != key) continue;
    bool valid = o.values.isEmpty();
    for (const auto& v : std::as_const(o.values)) valid = valid || v.value == value;
    if (!valid) return false;
    o.currentValue = value;
    m_overrides.insert(key, value);
    m_optionsDirty = true;
    return true;
  }
  if (!m_options.isEmpty()) return false;  // options known, key not among them
  m_overrides.insert(key, value);          // before registration: remember
  return true;
}

void LibretroBackend::setOptionVisible(const QString& key, bool visible) {
  QMutexLocker l(&m_optMutex);
  for (CoreOption& o : m_options)
    if (o.key == key) o.visible = visible;
}

void LibretroBackend::registerOptionsV2(const void* options) {
  const auto* o = static_cast<const retro_core_options_v2*>(options);
  QList<CoreOption> list;
  QList<CoreOptionCategory> cats;
  if (o->categories) {
    for (const auto* c = o->categories; c->key; ++c)
      cats.append({fromC(c->key), fromC(c->desc), fromC(c->info)});
  }
  for (const auto* d = o->definitions; d && d->key; ++d) {
    const bool cat = d->category_key && d->category_key[0];
    const QString desc = cat && d->desc_categorized ? fromC(d->desc_categorized) : fromC(d->desc);
    const QString info = cat && d->info_categorized ? fromC(d->info_categorized) : fromC(d->info);
    list.append(makeOption(d->key, desc, info, fromC(d->category_key), d->values, d->default_value));
  }
  QMutexLocker l(&m_optMutex);
  m_options = list;
  m_categories = cats;
  applyOverrides(m_options, m_overrides);
}

void LibretroBackend::registerOptionsV1(const void* definitions) {
  QList<CoreOption> list;
  for (const auto* d = static_cast<const retro_core_option_definition*>(definitions); d && d->key; ++d)
    list.append(makeOption(d->key, fromC(d->desc), fromC(d->info), QString(), d->values, d->default_value));
  QMutexLocker l(&m_optMutex);
  m_options = list;
  m_categories.clear();
  applyOverrides(m_options, m_overrides);
}

void LibretroBackend::registerOptionsV0(const void* variables) {
  QList<CoreOption> list;
  for (const auto* v = static_cast<const retro_variable*>(variables); v && v->key; ++v) {
    // "Description; value1|value2|..." - the first value is the default.
    const QString spec = fromC(v->value);
    const int sep = spec.indexOf(QLatin1Char(';'));
    CoreOption o;
    o.key = fromC(v->key);
    o.description = sep >= 0 ? spec.left(sep) : spec;
    const QStringList vals = (sep >= 0 ? spec.mid(sep + 1) : QString()).trimmed().split(QLatin1Char('|'));
    for (const QString& s : vals) o.values.append({s, QString()});
    if (!o.values.isEmpty()) o.defaultValue = o.values.first().value;
    o.currentValue = o.defaultValue;
    list.append(o);
  }
  QMutexLocker l(&m_optMutex);
  m_options = list;
  m_categories.clear();
  applyOverrides(m_options, m_overrides);
}

// ---------------------------------------------------------------- Callbacks

bool LibretroBackend::environmentCb(unsigned cmd, void* data) {
  return s_active && s_active->handleEnvironment(cmd, data);
}

void LibretroBackend::videoRefreshCb(const void* data, unsigned w, unsigned h, size_t pitch) {
  if (s_active) s_active->handleVideo(data, w, h, pitch);
}

void LibretroBackend::audioSampleCb(int16_t l, int16_t r) {
  if (!s_active) return;
  const int16_t s[2] = {l, r};
  s_active->m_audio.append(reinterpret_cast<const char*>(s), sizeof s);
}

size_t LibretroBackend::audioBatchCb(const int16_t* data, size_t frames) {
  if (s_active && data) s_active->m_audio.append(reinterpret_cast<const char*>(data), static_cast<qsizetype>(frames * 4));
  return frames;
}

void LibretroBackend::inputPollCb() {
  if (!s_active) return;
  QMutexLocker l(&s_active->m_inputMutex);
  s_active->m_polled = s_active->m_input;
}

int16_t LibretroBackend::inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id) {
  if (!s_active) return 0;
  const InputState& in = s_active->m_polled;
  switch (device & RETRO_DEVICE_MASK) {
    case RETRO_DEVICE_JOYPAD: {
      if (port > 1) return 0;
      const quint32 m = in.joypad[port];
      if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return static_cast<int16_t>(m & 0xFFFFu);
      return id < 16 ? static_cast<int16_t>((m >> id) & 1u) : 0;
    }
    case RETRO_DEVICE_POINTER: {
      if (port != 0 || index != 0) return 0;
      auto conv = [](double v) { return static_cast<int16_t>(std::lround((v * 2.0 - 1.0) * 0x7fff)); };
      switch (id) {
        case RETRO_DEVICE_ID_POINTER_X: return conv(in.px);
        case RETRO_DEVICE_ID_POINTER_Y: return conv(in.py);
        case RETRO_DEVICE_ID_POINTER_PRESSED: return in.pressed ? 1 : 0;
        case RETRO_DEVICE_ID_POINTER_COUNT: return in.pressed ? 1 : 0;
        default: return 0;
      }
    }
    default: return 0;
  }
}

void LibretroBackend::handleVideo(const void* data, unsigned width, unsigned height, size_t pitch) {
  if (!m_videoWanted.load()) {  // frame is not shown: no readback, no conversion; the last image stays
    ++m_frameCount;
    return;
  }
  if (data == RETRO_HW_FRAME_BUFFER_VALID && m_hwActive && width != 0 && height != 0) {
    const auto t0 = std::chrono::steady_clock::now();
    const QImage img = m_hw->readback(static_cast<int>(width), static_cast<int>(height), m_hwBottomLeft);
    m_lastReadbackNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    ++m_hwReadbacks;
    if (!img.isNull()) m_frame = img;
    ++m_frameCount;
    return;
  }
  if (!data || data == RETRO_HW_FRAME_BUFFER_VALID || width == 0 || height == 0) {
    ++m_frameCount;  // duplicate frame: last image stays
    return;
  }
  QImage img(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGB32);
  const auto* src = static_cast<const uchar*>(data);
  for (unsigned y = 0; y < height; ++y, src += pitch) {
    auto* dst = reinterpret_cast<quint32*>(img.scanLine(static_cast<int>(y)));
    if (m_pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
      const auto* s = reinterpret_cast<const quint32*>(src);
      for (unsigned x = 0; x < width; ++x) dst[x] = s[x] | 0xFF000000u;
    } else {
      const auto* s = reinterpret_cast<const quint16*>(src);
      for (unsigned x = 0; x < width; ++x) {
        const quint32 p = s[x];
        quint32 r, g, b;
        if (m_pixelFormat == RETRO_PIXEL_FORMAT_RGB565) {
          r = (p >> 11) & 0x1F; g = (p >> 5) & 0x3F; b = p & 0x1F;
          r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
        } else {  // 0RGB1555
          r = (p >> 10) & 0x1F; g = (p >> 5) & 0x1F; b = p & 0x1F;
          r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
        }
        dst[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
      }
    }
  }
  m_frame = img;
  ++m_frameCount;
}

bool LibretroBackend::handleEnvironment(unsigned rawCmd, void* data) {
  const unsigned cmd = rawCmd & ~(RETRO_ENVIRONMENT_EXPERIMENTAL | RETRO_ENVIRONMENT_PRIVATE);
  switch (cmd) {
    case RETRO_ENVIRONMENT_SET_ROTATION: return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *static_cast<bool*>(data) = true; return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: {
      if (const auto* m = static_cast<const retro_message*>(data)) {
        QString t = fromC(m->msg);
        if (dedupeLog(t)) qCInfo(lcCore).noquote() << t;
      }
      return true;
    }
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
      if (const auto* m = static_cast<const retro_message_ext*>(data)) {
        QString t = fromC(m->msg);
        if (dedupeLog(t)) qCInfo(lcCore).noquote() << t;
      }
      return true;
    }
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION: *static_cast<unsigned*>(data) = 1; return true;
    case RETRO_ENVIRONMENT_SHUTDOWN: m_shutdownRequested = true; return true;
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      if (m_systemDir.isEmpty()) return false;
      *static_cast<const char**>(data) = m_systemDir.constData();
      return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      if (m_saveDir.isEmpty()) return false;
      *static_cast<const char**>(data) = m_saveDir.constData();
      return true;
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
      *static_cast<const char**>(data) = m_corePathUtf8.constData();
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
      const int fmt = *static_cast<const int*>(data);
      if (fmt != RETRO_PIXEL_FORMAT_0RGB1555 && fmt != RETRO_PIXEL_FORMAT_XRGB8888 && fmt != RETRO_PIXEL_FORMAT_RGB565)
        return false;
      m_pixelFormat = fmt;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: return true;
    case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
      *static_cast<uint64_t*>(data) = (1ull << RETRO_DEVICE_JOYPAD) | (1ull << RETRO_DEVICE_POINTER);
      return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS & ~RETRO_ENVIRONMENT_EXPERIMENTAL: return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      static_cast<retro_log_callback*>(data)->log = &coreLog;
      return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS: return true;
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE: return true;
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: return true;
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO: return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS & ~RETRO_ENVIRONMENT_EXPERIMENTAL: return false;  // unused by the Player (was dead code before masking)
    case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE: {
      // Only inhibit_toggle matters: the user's chosen speed always wins over the core's ratio.
      if (data) m_ffInhibit.store(static_cast<const retro_fastforwarding_override*>(data)->inhibit_toggle);
      return true;
    }
    // rawCmd is masked: the experimental flag is not part of the case labels.
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING & ~RETRO_ENVIRONMENT_EXPERIMENTAL:
      *static_cast<bool*>(data) = m_fastForwarding.load();
      return true;
    case RETRO_ENVIRONMENT_GET_THROTTLE_STATE & ~RETRO_ENVIRONMENT_EXPERIMENTAL: {
      auto* t = static_cast<retro_throttle_state*>(data);
      const double fps = m_av.fps > 1.0 ? m_av.fps : 60.0;
      if (m_fastForwarding.load()) {
        t->mode = RETRO_THROTTLE_FAST_FORWARD;
        t->rate = static_cast<float>(fps * m_ffRatio.load());
      } else {
        t->mode = RETRO_THROTTLE_NONE;
        t->rate = static_cast<float>(fps);
      }
      return true;
    }
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE & ~RETRO_ENVIRONMENT_EXPERIMENTAL:
      // bit 0 = video, bit 1 = audio: audio is always on; video only when the frame will be shown
      if (data) *static_cast<int*>(data) = m_videoWanted.load() ? 3 : 2;
      return true;
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE & ~RETRO_ENVIRONMENT_EXPERIMENTAL: *static_cast<float*>(data) = static_cast<float>(m_av.fps); return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH; return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
      const auto* g = static_cast<const retro_game_geometry*>(data);
      m_av.width = static_cast<int>(g->base_width);
      m_av.height = static_cast<int>(g->base_height);
      m_av.aspectRatio = static_cast<double>(g->aspect_ratio);
      if (m_hwActive && m_hw->makeCurrent())
        return m_hw->ensureSize(static_cast<int>(g->max_width), static_cast<int>(g->max_height));
      return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
      const auto* av = static_cast<const retro_system_av_info*>(data);
      m_av.width = static_cast<int>(av->geometry.base_width);
      m_av.height = static_cast<int>(av->geometry.base_height);
      m_av.aspectRatio = static_cast<double>(av->geometry.aspect_ratio);
      m_av.fps = av->timing.fps;
      m_av.sampleRate = av->timing.sample_rate;
      if (m_hwActive && m_hw->makeCurrent())
        return m_hw->ensureSize(static_cast<int>(av->geometry.max_width), static_cast<int>(av->geometry.max_height));
      return true;
    }

    // Core Options
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *static_cast<unsigned*>(data) = 2; return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: registerOptionsV2(data); return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
      registerOptionsV2(static_cast<const retro_core_options_v2_intl*>(data)->us);
      return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: registerOptionsV1(data); return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
      registerOptionsV1(static_cast<const retro_core_options_intl*>(data)->us);
      return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: registerOptionsV0(data); return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY: {
      if (const auto* d = static_cast<const retro_core_option_display*>(data)) setOptionVisible(fromC(d->key), d->visible);
      return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK: return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      auto* var = static_cast<retro_variable*>(data);
      if (!var || !var->key) return false;
      QMutexLocker l(&m_optMutex);
      const QString key = fromC(var->key);
      for (const CoreOption& o : std::as_const(m_options)) {
        if (o.key != key) continue;
        std::string& slot = m_varCache[var->key];
        slot = o.currentValue.toStdString();
        var->value = slot.c_str();
        return true;
      }
      return false;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLE: {
      const auto* var = static_cast<const retro_variable*>(data);
      if (!var) return true;  // query whether supported
      if (!var->key || !var->value) return false;
      QMutexLocker l(&m_optMutex);
      for (CoreOption& o : m_options) {
        if (o.key != fromC(var->key)) continue;
        o.currentValue = fromC(var->value);
        m_overrides.insert(o.key, o.currentValue);
        return true;
      }
      return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
      QMutexLocker l(&m_optMutex);
      *static_cast<bool*>(data) = m_optionsDirty;
      m_optionsDirty = false;
      return true;
    }

    // Hardware rendering (OpenGL / OpenGL Core only, see setupHwRender)
    case RETRO_ENVIRONMENT_SET_HW_RENDER: return setupHwRender(data);
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: {
      if (!HwRenderContext::allowed()) return false;
      if (!m_hw) m_hw = std::make_unique<HwRenderContext>();
      if (!m_hw->available()) return false;
      *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGL_CORE;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT & ~RETRO_ENVIRONMENT_EXPERIMENTAL: return false;  // no shared contexts offered

    // Deliberately rejected: other HW APIs, rumble, sensors, VFS, microphone, netpacket, ...
    default: return false;
  }
}

}  // namespace framebeam::emu
