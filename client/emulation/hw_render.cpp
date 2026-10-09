#include "hw_render.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <vector>

namespace framebeam::emu {

namespace {
Q_LOGGING_CATEGORY(lcHw, "framebeam.emulation.hw")

constexpr int kInitialSize = 640;

QString glString(QOpenGLFunctions* f, GLenum name) {
  const auto* s = f->glGetString(name);
  return s ? QString::fromLatin1(reinterpret_cast<const char*>(s)) : QStringLiteral("?");
}

// Restores the GL bindings that this class touches, so the core's own state stays intact.
struct BindingGuard {
  explicit BindingGuard(QOpenGLFunctions* fn) : f(fn) {
    GLint v = 0;
    f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &v);
    fbo = static_cast<GLuint>(v);
    f->glGetIntegerv(GL_RENDERBUFFER_BINDING, &v);
    rbo = static_cast<GLuint>(v);
    f->glGetIntegerv(GL_TEXTURE_BINDING_2D, &v);
    tex = static_cast<GLuint>(v);
    f->glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &v);
    pbo = static_cast<GLuint>(v);
    f->glGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);
  }
  ~BindingGuard() {
    f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    f->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    f->glBindTexture(GL_TEXTURE_2D, tex);
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    f->glPixelStorei(GL_PACK_ALIGNMENT, packAlign);
  }
  Q_DISABLE_COPY(BindingGuard)
  QOpenGLFunctions* f;
  GLuint fbo = 0, rbo = 0, tex = 0, pbo = 0;
  GLint packAlign = 4;
};
}  // namespace

struct HwRenderContext::Impl {
  QOffscreenSurface* surface = nullptr;
  std::unique_ptr<QOpenGLContext> ctx;
  QOpenGLFunctions* f = nullptr;
  QOpenGLExtraFunctions* fx = nullptr;  // glMapBufferRange/glUnmapBuffer (GL 3.0 core, ES 3)
  GLuint fbo = 0, tex = 0, rbo = 0;
  // Halving chain for readbacks under a size limit (color only): level i holds the frame at 1/2^(i+1) size.
  struct Level {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
  };
  std::vector<Level> levels;
  bool scaleFailed = false;  // blit FBO unusable: full-size readback from now on
  bool depth = false, stencil = false;
  int w = 0, h = 0;
  QString info;
  QString renderer, version;
  bool coreProfile = false;
  std::vector<uchar> buf;

  // Asynchronous readback: two pixel-pack buffers used alternately (ping-pong). Frame N is read into
  // slot N and the previous frame (slot N-1) is mapped, so the emulation thread never waits for the GPU.
  struct Slot {
    GLuint pbo = 0;
    qsizetype capacity = 0;  // bytes allocated
    int w = 0, h = 0;        // dimensions of the pending frame (0 = none)
    bool bottomLeft = true;
  };
  Slot pboSlots[2];
  int cur = 0;
  bool async = true;

  void releasePbos() {
    for (Slot& s : pboSlots) {
      if (s.pbo && f) f->glDeleteBuffers(1, &s.pbo);
      s = Slot{};
    }
    cur = 0;
  }

  // Converts RGBA bytes (GL order) into an RGB32 image; bottomLeftOrigin flips vertically.
  static QImage convert(const uchar* src, int w, int h, bool bottomLeftOrigin) {
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
      const int srcY = bottomLeftOrigin ? h - 1 - y : y;
      const uchar* s = src + static_cast<size_t>(srcY) * static_cast<size_t>(w) * 4;
      auto* dst = reinterpret_cast<quint32*>(img.scanLine(y));
      for (int x = 0; x < w; ++x, s += 4)
        dst[x] = 0xFF000000u | (quint32(s[0]) << 16) | (quint32(s[1]) << 8) | quint32(s[2]);
    }
    return img;
  }

  // Maps the slot's PBO (must be bound as GL_PIXEL_PACK_BUFFER) and converts its pending frame; null on failure.
  QImage mapSlot(const Slot& s) {
    if (!s.pbo || s.w <= 0) return {};
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
    const auto* p = static_cast<const uchar*>(
        fx->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(s.w) * s.h * 4, GL_MAP_READ_BIT));
    if (!p) return {};
    const QImage img = convert(p, s.w, s.h, s.bottomLeft);
    fx->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    return img;
  }

  // Issues the read of the FBO into the current slot's PBO. false: PBO unusable (caller falls back).
  bool readIntoSlot(Slot& s, int w, int h) {
    if (!s.pbo) f->glGenBuffers(1, &s.pbo);
    if (!s.pbo) return false;
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
    const qsizetype need = static_cast<qsizetype>(w) * h * 4;
    if (need > s.capacity) {
      f->glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(need), nullptr, GL_STREAM_READ);
      s.capacity = need;
    }
    while (f->glGetError() != GL_NO_ERROR) {}  // stale errors from the core must not look like ours
    f->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    return f->glGetError() == GL_NO_ERROR;
  }

  void releaseScaled() {
    for (Level& l : levels) {
      if (f && l.fbo) f->glDeleteFramebuffers(1, &l.fbo);
      if (f && l.tex) f->glDeleteTextures(1, &l.tex);
    }
    levels.clear();
  }

  // (Re)creates halving level `i` at tw x th if needed; levels are cached per size, so a steady stream of
  // equally sized frames never reallocates. false: not possible (scaleFailed is set).
  bool ensureLevel(size_t i, int tw, int th) {
    if (levels.size() <= i) levels.resize(i + 1);
    Level& l = levels[i];
    if (l.fbo && l.w == tw && l.h == th) return true;
    BindingGuard guard(f);
    if (!l.fbo) f->glGenFramebuffers(1, &l.fbo);
    if (!l.tex) f->glGenTextures(1, &l.tex);
    while (f->glGetError() != GL_NO_ERROR) {}
    f->glBindTexture(GL_TEXTURE_2D, l.tex);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    f->glBindFramebuffer(GL_FRAMEBUFFER, l.fbo);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, l.tex, 0);
    if (f->glGetError() != GL_NO_ERROR || f->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      releaseScaled();
      scaleFailed = true;
      return false;
    }
    l.w = tw;
    l.h = th;
    return true;
  }

  // Downscales the w x h region of the main FBO to tw x th: `steps` exact 2:1 GL_LINEAR blits (each output pixel
  // is the average of a 2x2 block: a box filter), then, unless the chain already ends at tw x th, one final
  // GL_LINEAR blit with a ratio below 2:1 (every source texel still contributes). Odd sizes crop the last
  // row/column of a halving step. The first blit flips vertically when `flip`. Leaves the final level bound as
  // GL_FRAMEBUFFER on success.
  bool blitScaled(int w, int h, int steps, int tw, int th, bool flip) {
    if (!fx) return false;
    const bool finalStep = (w >> steps) != tw || (h >> steps) != th;
    const int stages = steps + (finalStep ? 1 : 0);
    if (stages <= 0) return false;
    const GLboolean scissor = f->glIsEnabled(GL_SCISSOR_TEST);  // the core may leave it on; a blit honours it
    if (scissor) f->glDisable(GL_SCISSOR_TEST);
    GLuint src = fbo;
    int sw = w, sh = h;
    bool ok = true;
    for (int i = 0; i < stages && ok; ++i) {
      const bool halving = i < steps;
      const int dw = halving ? std::max(1, sw / 2) : tw, dh = halving ? std::max(1, sh / 2) : th;
      if (!ensureLevel(static_cast<size_t>(i), dw, dh)) {
        ok = false;
        break;
      }
      const int cw = halving ? std::min(sw, dw * 2) : sw, ch = halving ? std::min(sh, dh * 2) : sh;
      while (f->glGetError() != GL_NO_ERROR) {}
      f->glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
      f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, levels[static_cast<size_t>(i)].fbo);
      const bool fl = flip && i == 0;
      fx->glBlitFramebuffer(0, 0, cw, ch, 0, fl ? dh : 0, dw, fl ? 0 : dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
      ok = f->glGetError() == GL_NO_ERROR;
      src = levels[static_cast<size_t>(i)].fbo;
      sw = dw;
      sh = dh;
    }
    if (scissor) f->glEnable(GL_SCISSOR_TEST);
    if (!ok) {
      releaseScaled();
      scaleFailed = true;
      return false;
    }
    f->glBindFramebuffer(GL_FRAMEBUFFER, src);
    return true;
  }

  bool allocate(int nw, int nh) {
    BindingGuard guard(f);
    f->glBindTexture(GL_TEXTURE_2D, tex);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, nw, nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (rbo) {
      f->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
      const GLenum fmt = stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
      f->glRenderbufferStorage(GL_RENDERBUFFER, fmt, nw, nh);
      f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, rbo);
    }
    if (f->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
    w = nw;
    h = nh;
    return true;
  }
};

HwRenderContext::HwRenderContext() : d(std::make_unique<Impl>()) {}

HwRenderContext::~HwRenderContext() {
  destroyContext();
  if (d->surface) {
    // Created on the GUI thread; destroy it there.
    if (QThread::currentThread() == d->surface->thread()) delete d->surface;
    else d->surface->deleteLater();
  }
}

bool HwRenderContext::allowed() {
  if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr) return false;
  return qEnvironmentVariable("FRAMEBEAM_DISABLE_HW_RENDER") != QLatin1String("1");
}

void HwRenderContext::prepareSurface() {
  if (d->surface || !allowed()) return;
  auto* s = new QOffscreenSurface();
  s->create();
  if (!s->isValid()) {
    qCWarning(lcHw) << "Offscreen surface could not be created; hardware rendering unavailable";
    delete s;
    return;
  }
  d->surface = s;
}

bool HwRenderContext::available() {
  if (!allowed()) return false;
  if (!d->surface && QThread::currentThread() == QCoreApplication::instance()->thread()) prepareSurface();
  return d->surface != nullptr;
}

bool HwRenderContext::createContext(bool coreProfile, unsigned major, unsigned minor, bool depth, bool stencil,
                                    QString* error) {
  auto fail = [&](const QString& msg) {
    if (error) *error = msg;
    destroyContext();
    return false;
  };
  if (!available()) return fail(QStringLiteral("no offscreen surface"));
  destroyContext();

  QSurfaceFormat fmt;
  fmt.setRenderableType(QSurfaceFormat::OpenGL);
  fmt.setRedBufferSize(8);
  fmt.setGreenBufferSize(8);
  fmt.setBlueBufferSize(8);
  fmt.setAlphaBufferSize(8);
  int reqMajor = 0, reqMinor = 0;  // effective requested version (0.0 = any)
  if (coreProfile) {
    unsigned ma = std::max(major, 3u), mi = minor;
    if (ma == 3 && mi < 2) mi = 2;  // a core profile needs at least 3.2
    reqMajor = static_cast<int>(ma);
    reqMinor = static_cast<int>(mi);
    fmt.setVersion(reqMajor, reqMinor);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
  } else {
    fmt.setProfile(QSurfaceFormat::NoProfile);
    if (major != 0) {
      reqMajor = static_cast<int>(major);
      reqMinor = static_cast<int>(minor);
      fmt.setVersion(reqMajor, reqMinor);
    }
  }
  d->ctx = std::make_unique<QOpenGLContext>();
  d->ctx->setFormat(fmt);
  if (!d->ctx->create()) return fail(QStringLiteral("OpenGL context could not be created"));
  const QSurfaceFormat got = d->ctx->format();
  if (coreProfile && got.profile() != QSurfaceFormat::CoreProfile)
    return fail(QStringLiteral("Core profile not available (got %1.%2)").arg(got.majorVersion()).arg(got.minorVersion()));
  if (!d->ctx->makeCurrent(d->surface)) return fail(QStringLiteral("OpenGL context could not be made current"));
  d->f = d->ctx->functions();
  d->f->initializeOpenGLFunctions();
  d->fx = d->ctx->extraFunctions();
  if (d->fx) d->fx->initializeOpenGLFunctions();
  d->async = d->fx != nullptr && qEnvironmentVariable("FRAMEBEAM_SYNC_READBACK") != QLatin1String("1");
  {
    // QSurfaceFormat may echo the request; the driver's real version is authoritative.
    GLint ma = 0, mi = 0;
    d->f->glGetIntegerv(GL_MAJOR_VERSION, &ma);
    d->f->glGetIntegerv(GL_MINOR_VERSION, &mi);
    if (ma == 0) {
      ma = got.majorVersion();
      mi = got.minorVersion();
    }
    if (ma < reqMajor || (ma == reqMajor && mi < reqMinor))
      return fail(QStringLiteral("OpenGL %1.%2 requested, got %3.%4").arg(reqMajor).arg(reqMinor).arg(ma).arg(mi));
  }
  d->renderer = glString(d->f, GL_RENDERER);
  d->version = glString(d->f, GL_VERSION);
  d->coreProfile = coreProfile;
  d->info = QStringLiteral("%1 / %2 / %3")
                .arg(glString(d->f, GL_VENDOR), glString(d->f, GL_RENDERER), glString(d->f, GL_VERSION));

  d->depth = depth || stencil;
  d->stencil = stencil;
  d->f->glGenFramebuffers(1, &d->fbo);
  d->f->glGenTextures(1, &d->tex);
  if (d->depth) d->f->glGenRenderbuffers(1, &d->rbo);
  if (!d->allocate(kInitialSize, kInitialSize)) return fail(QStringLiteral("Framebuffer is incomplete"));
  qCInfo(lcHw).noquote() << "OpenGL context" << (coreProfile ? "core" : "compat") << got.majorVersion() << "." << got.minorVersion()
                         << ":" << d->info;
  return true;
}

void HwRenderContext::destroyContext() {
  if (d->ctx) {
    if (d->f && d->ctx->makeCurrent(d->surface)) {
      d->releasePbos();
      d->releaseScaled();
      if (d->fbo) d->f->glDeleteFramebuffers(1, &d->fbo);
      if (d->tex) d->f->glDeleteTextures(1, &d->tex);
      if (d->rbo) d->f->glDeleteRenderbuffers(1, &d->rbo);
      d->ctx->doneCurrent();
    }
    d->ctx.reset();
  }
  d->f = nullptr;
  d->fx = nullptr;
  d->pboSlots[0] = d->pboSlots[1] = {};
  d->fbo = d->tex = d->rbo = 0;
  d->scaleFailed = false;
  d->w = d->h = 0;
  d->buf.clear();
}

bool HwRenderContext::hasContext() const { return d->ctx != nullptr; }

bool HwRenderContext::makeCurrent() {
  if (!d->ctx || !d->surface) return false;
  // Already current (the normal case: one context per emulation thread): no driver call on every frame.
  if (QOpenGLContext::currentContext() == d->ctx.get()) return true;
  return d->ctx->makeCurrent(d->surface);
}
void HwRenderContext::doneCurrent() {
  if (d->ctx) d->ctx->doneCurrent();
}

bool HwRenderContext::ensureSize(int w, int h) {
  if (!d->ctx) return false;
  if (w <= d->w && h <= d->h) return true;
  const int nw = std::max(w, d->w), nh = std::max(h, d->h);
  if (!d->allocate(nw, nh)) {
    qCWarning(lcHw) << "Could not resize the framebuffer to" << nw << "x" << nh;
    return false;
  }
  return true;
}

quintptr HwRenderContext::framebuffer() const { return d->fbo; }

HwRenderContext::ProcAddress HwRenderContext::procAddress(const char* name) const {
  return d->ctx ? d->ctx->getProcAddress(name) : nullptr;
}

QString HwRenderContext::glInfo() const { return d->info; }
QString HwRenderContext::glRenderer() const { return d->renderer; }
QString HwRenderContext::glVersion() const { return d->version; }
bool HwRenderContext::isCoreProfile() const { return d->coreProfile; }

namespace {
// Splits "4.5 (Core Profile) Mesa 23.2.1" into the leading "major.minor" and the vendor/driver rest.
void splitGlVersion(const QString& v, QString* number, QString* rest) {
  const QString t = v.trimmed();
  qsizetype i = 0;
  while (i < t.size() && (t.at(i).isDigit() || t.at(i) == QLatin1Char('.'))) ++i;
  const QStringList parts = t.left(i).split(QLatin1Char('.'), Qt::SkipEmptyParts);
  *number = parts.size() >= 2 ? parts.at(0) + QLatin1Char('.') + parts.at(1) : t.left(i);
  QString r = t.mid(i).trimmed();
  while (r.startsWith(QLatin1Char('('))) {
    const qsizetype close = r.indexOf(QLatin1Char(')'));
    if (close < 0) break;
    r = r.mid(close + 1).trimmed();
  }
  *rest = r;
}
}  // namespace

QString HwRenderContext::describeApi(const QString& glVersion, bool coreProfile) {
  QString number, rest;
  splitGlVersion(glVersion, &number, &rest);
  const QString base = number.isEmpty() ? QStringLiteral("OpenGL") : QStringLiteral("OpenGL ") + number;
  return coreProfile ? base + QStringLiteral(" Core") : base;
}

QString HwRenderContext::describeGpu(const QString& glRenderer, const QString& glVersion) {
  QString number, rest;
  splitGlVersion(glVersion, &number, &rest);
  const QString gpu = glRenderer.trimmed().isEmpty() ? QStringLiteral("GPU") : glRenderer.trimmed();
  return rest.isEmpty() ? gpu : gpu + QStringLiteral(" · Driver ") + rest;
}

bool HwRenderContext::asyncReadback() const { return d->async; }

QSize HwRenderContext::scaledReadbackSize(int w, int h, const QSize& maxSize) {
  if (w <= 0 || h <= 0 || maxSize.width() <= 0 || maxSize.height() <= 0) return QSize(w, h);
  if (w <= maxSize.width() && h <= maxSize.height()) return QSize(w, h);  // never upscale
  const double scale = std::min(static_cast<double>(maxSize.width()) / w, static_cast<double>(maxSize.height()) / h);
  const int tw = std::clamp(static_cast<int>(std::lround(w * scale)), 1, maxSize.width());
  const int th = std::clamp(static_cast<int>(std::lround(h * scale)), 1, maxSize.height());
  return QSize(tw, th);
}

namespace {
// Exact 2:1 halvings before the final blit: the most that keep the result >= the target in both dimensions.
int halvingSteps(int w, int h, const QSize& target) {
  int k = 0;
  while (k < 30 && (w >> (k + 1)) >= std::max(1, target.width()) && (h >> (k + 1)) >= std::max(1, target.height())) ++k;
  return k;
}
}  // namespace

QImage HwRenderContext::readback(int w, int h, bool bottomLeftOrigin, const QSize& maxSize) {
  if (!d->ctx || w <= 0 || h <= 0 || w > d->w || h > d->h) return {};
  BindingGuard guard(d->f);
  d->f->glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
  d->f->glPixelStorei(GL_PACK_ALIGNMENT, 4);

  // Downscale on the GPU first when the frame is larger than the consumers need.
  int rw = w, rh = h;
  bool bottomLeft = bottomLeftOrigin;
  if (!d->scaleFailed) {
    const QSize t = scaledReadbackSize(w, h, maxSize);
    if (t != QSize(w, h)) {
      if (d->blitScaled(w, h, halvingSteps(w, h, t), t.width(), t.height(), bottomLeftOrigin)) {
        rw = t.width();
        rh = t.height();
        bottomLeft = false;  // the blit already flipped
      } else {
        qCWarning(lcHw) << "GPU downscale unavailable; reading back full-size frames";
        d->f->glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
      }
    }
  }

  if (d->async) {
    Impl::Slot& cur = d->pboSlots[d->cur];
    Impl::Slot& prev = d->pboSlots[1 - d->cur];
    if (d->readIntoSlot(cur, rw, rh)) {
      cur.w = rw;
      cur.h = rh;
      cur.bottomLeft = bottomLeft;
      // The first frame (nothing pending) is returned synchronously so it is never empty; later calls map the
      // previous frame, which the GPU finished long ago (one frame of latency, no stall).
      const bool first = prev.w == 0;
      QImage img = d->mapSlot(first ? cur : prev);
      d->f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      if (!img.isNull()) {
        d->cur = 1 - d->cur;
        return img;
      }
      qCWarning(lcHw) << "Mapping the readback buffer failed; falling back to synchronous readback";
    } else {
      qCWarning(lcHw) << "Asynchronous readback unavailable; falling back to synchronous readback";
    }
    d->async = false;
    d->releasePbos();
  }

  d->f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  d->buf.resize(static_cast<size_t>(rw) * static_cast<size_t>(rh) * 4);
  d->f->glReadPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, d->buf.data());
  return Impl::convert(d->buf.data(), rw, rh, bottomLeft);
}

}  // namespace framebeam::emu
