#include "hw_render.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QThread>

#include <algorithm>
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
  }
  ~BindingGuard() {
    f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    f->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    f->glBindTexture(GL_TEXTURE_2D, tex);
  }
  Q_DISABLE_COPY(BindingGuard)
  QOpenGLFunctions* f;
  GLuint fbo = 0, rbo = 0, tex = 0;
};
}  // namespace

struct HwRenderContext::Impl {
  QOffscreenSurface* surface = nullptr;
  std::unique_ptr<QOpenGLContext> ctx;
  QOpenGLFunctions* f = nullptr;
  GLuint fbo = 0, tex = 0, rbo = 0;
  bool depth = false, stencil = false;
  int w = 0, h = 0;
  QString info;
  std::vector<uchar> buf;

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
  if (coreProfile) {
    unsigned ma = std::max(major, 3u), mi = minor;
    if (ma == 3 && mi < 2) mi = 2;  // a core profile needs at least 3.2
    fmt.setVersion(static_cast<int>(ma), static_cast<int>(mi));
    fmt.setProfile(QSurfaceFormat::CoreProfile);
  } else {
    fmt.setProfile(QSurfaceFormat::NoProfile);
    if (major != 0) fmt.setVersion(static_cast<int>(major), static_cast<int>(minor));
  }
  d->ctx = std::make_unique<QOpenGLContext>();
  d->ctx->setFormat(fmt);
  if (!d->ctx->create()) return fail(QStringLiteral("OpenGL context could not be created"));
  const QSurfaceFormat got = d->ctx->format();
  if (coreProfile && (got.version() < qMakePair(3, 2) || got.profile() != QSurfaceFormat::CoreProfile))
    return fail(QStringLiteral("Core profile not available (got %1.%2)").arg(got.majorVersion()).arg(got.minorVersion()));
  if (!d->ctx->makeCurrent(d->surface)) return fail(QStringLiteral("OpenGL context could not be made current"));
  d->f = d->ctx->functions();
  d->f->initializeOpenGLFunctions();
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
      if (d->fbo) d->f->glDeleteFramebuffers(1, &d->fbo);
      if (d->tex) d->f->glDeleteTextures(1, &d->tex);
      if (d->rbo) d->f->glDeleteRenderbuffers(1, &d->rbo);
      d->ctx->doneCurrent();
    }
    d->ctx.reset();
  }
  d->f = nullptr;
  d->fbo = d->tex = d->rbo = 0;
  d->w = d->h = 0;
  d->buf.clear();
}

bool HwRenderContext::hasContext() const { return d->ctx != nullptr; }

bool HwRenderContext::makeCurrent() { return d->ctx && d->surface && d->ctx->makeCurrent(d->surface); }
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

QImage HwRenderContext::readback(int w, int h, bool bottomLeftOrigin) {
  if (!d->ctx || w <= 0 || h <= 0 || w > d->w || h > d->h) return {};
  BindingGuard guard(d->f);
  d->f->glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
  d->f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  d->f->glPixelStorei(GL_PACK_ALIGNMENT, 4);
  d->buf.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  d->f->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, d->buf.data());
  QImage img(w, h, QImage::Format_RGB32);
  for (int y = 0; y < h; ++y) {
    const int srcY = bottomLeftOrigin ? h - 1 - y : y;
    const uchar* s = d->buf.data() + static_cast<size_t>(srcY) * static_cast<size_t>(w) * 4;
    auto* dst = reinterpret_cast<quint32*>(img.scanLine(y));
    for (int x = 0; x < w; ++x, s += 4)
      dst[x] = 0xFF000000u | (quint32(s[0]) << 16) | (quint32(s[1]) << 8) | quint32(s[2]);
  }
  return img;
}

}  // namespace framebeam::emu
