#pragma once
// Test double for the zero-copy display (ADR 0027): the producer side as a plain class, wrapped by thin adapters in the
// tests into emu::GpuDisplayTarget (emulation tests) and framebeam::DisplayProducer (UI tests). It owns a real GL
// texture that is LARGER than requested (so top-left addressing is tested), enforces the acquire/present/abort pairing
// and the GL-current rule, reads the texture back with glReadPixels on present() (rows as stored: image row y = GL row y,
// which is the contract's "image top row = GL row 0") and records every call. Thread-safe: the emulation thread calls
// it, the test thread inspects it.
#include <QImage>
#include <QList>
#include <QMutex>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSize>
#include <QString>
#include <QStringList>

#include <atomic>

namespace fbtest {

class FakeGlDisplay {
 public:
  enum class Status { Ok, Skip, NotReady, Unavailable, Failed };
  struct Present {
    QImage image;  // the w x h region as presented (RGB32)
    QSize size;
    unsigned tex = 0;
    QSize texSize;
  };

  // ---- knobs (any thread)
  std::atomic<bool> isWanted{true};
  std::atomic<int> padW{24}, padH{12};              // texture = requested size rounded up to a multiple of 16, plus this
  std::atomic<bool> lieAboutSize{false};            // acquire() reports a 1x1 texture (a blit into it must fail)
  std::atomic<int> nextStatus{static_cast<int>(Status::Ok)};  // returned by acquire() until changed

  // ---- the producer calls (emulation thread, GL current)
  Status acquire(int w, int h, unsigned* tex, QSize* texSize) {
    QMutexLocker l(&mutex_);
    calls_ << QStringLiteral("acquire");
    if (!QOpenGLContext::currentContext()) violations_ << QStringLiteral("acquire without a current GL context");
    const Status st = static_cast<Status>(nextStatus.load());
    if (st != Status::Ok) {
      calls_.last() = QStringLiteral("acquire (%1)").arg(statusName(st));
      return st;
    }
    if (locked_) violations_ << QStringLiteral("acquire while the texture is still locked");
    lastRequest_ = QSize(w, h);
    requests_ << QSize(w, h);
    const QSize need(((w + 15) & ~15) + padW.load(), ((h + 15) & ~15) + padH.load());
    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    if (tex_ == 0 || texSize_.width() < w || texSize_.height() < h) {
      if (tex_ != 0) f->glDeleteTextures(1, &tex_);  // grow-only: a new texture object, the old one goes
      f->glGenTextures(1, &tex_);
      GLint prev = 0;
      f->glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
      f->glBindTexture(GL_TEXTURE_2D, tex_);
      f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, need.width(), need.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      f->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev));
      texSize_ = need;
      ++textures_;
      clearToSentinel(f);
    }
    locked_ = true;
    *tex = tex_;
    *texSize = lieAboutSize.load() ? QSize(1, 1) : texSize_;
    return Status::Ok;
  }

  void present(int w, int h) {
    QMutexLocker l(&mutex_);
    calls_ << QStringLiteral("present");
    if (!locked_) violations_ << QStringLiteral("present without acquire");
    else if (QSize(w, h) != lastRequest_) violations_ << QStringLiteral("present %1x%2 after acquire %3x%4").arg(w).arg(h).arg(lastRequest_.width()).arg(lastRequest_.height());
    locked_ = false;
    if (!QOpenGLContext::currentContext() || tex_ == 0) return;
    Present p;
    p.size = QSize(w, h);
    p.tex = tex_;
    p.texSize = texSize_;
    p.image = readTexture(w, h);
    presents_ << p;
  }

  void abort() {
    QMutexLocker l(&mutex_);
    calls_ << QStringLiteral("abort");
    if (!locked_) violations_ << QStringLiteral("abort without acquire");
    locked_ = false;
  }

  void release(bool glCurrent) {
    QMutexLocker l(&mutex_);
    calls_ << (glCurrent ? QStringLiteral("release (GL current)") : QStringLiteral("release (no GL)"));
    ++releases_;
    lastReleaseGlCurrent_ = glCurrent;
    if (glCurrent && QOpenGLContext::currentContext() == nullptr) violations_ << QStringLiteral("release(true) without a GL context");
    if (locked_) violations_ << QStringLiteral("release while the texture is still locked");
    if (glCurrent && tex_ != 0 && QOpenGLContext::currentContext()) {
      QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
      textureAliveAtRelease_ = f->glIsTexture(tex_);
      f->glDeleteTextures(1, &tex_);
    }
    tex_ = 0;
    texSize_ = QSize();
  }

  void fail(const QString& reason) {
    QMutexLocker l(&mutex_);
    calls_ << QStringLiteral("fail");
    if (failReason_.isEmpty()) failReason_ = reason;
  }

  // ---- inspection (any thread)
  QStringList calls() const { QMutexLocker l(&mutex_); return calls_; }
  int count(const QString& prefix) const {
    QMutexLocker l(&mutex_);
    int n = 0;
    for (const QString& c : calls_) n += c.startsWith(prefix) ? 1 : 0;
    return n;
  }
  QList<Present> presents() const { QMutexLocker l(&mutex_); return presents_; }
  QImage lastImage() const { QMutexLocker l(&mutex_); return presents_.isEmpty() ? QImage() : presents_.last().image; }
  QList<QSize> requests() const { QMutexLocker l(&mutex_); return requests_; }
  QStringList violations() const { QMutexLocker l(&mutex_); return violations_; }
  QString failReason() const { QMutexLocker l(&mutex_); return failReason_; }
  int textures() const { QMutexLocker l(&mutex_); return textures_; }
  int releases() const { QMutexLocker l(&mutex_); return releases_; }
  bool lastReleaseGlCurrent() const { QMutexLocker l(&mutex_); return lastReleaseGlCurrent_; }
  bool textureAliveAtRelease() const { QMutexLocker l(&mutex_); return textureAliveAtRelease_; }
  bool locked() const { QMutexLocker l(&mutex_); return locked_; }

  static QString statusName(Status s) {
    switch (s) {
      case Status::Ok: return QStringLiteral("ok");
      case Status::Skip: return QStringLiteral("skip");
      case Status::NotReady: return QStringLiteral("not ready");
      case Status::Unavailable: return QStringLiteral("unavailable");
      case Status::Failed: return QStringLiteral("failed");
    }
    return {};
  }

 private:
  // The sentinel is opaque magenta: a blit that wrote outside the top-left region would show.
  void clearToSentinel(QOpenGLFunctions* f) {
    GLint prevRead = 0, prevDraw = 0;
    f->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    f->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    GLuint fbo = 0;
    f->glGenFramebuffers(1, &fbo);
    f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
    GLfloat cc[4] = {0, 0, 0, 0};
    f->glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
    const GLboolean scissor = f->glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) f->glDisable(GL_SCISSOR_TEST);
    f->glClearColor(1.f, 0.f, 1.f, 1.f);
    f->glClear(GL_COLOR_BUFFER_BIT);
    f->glClearColor(cc[0], cc[1], cc[2], cc[3]);
    if (scissor) f->glEnable(GL_SCISSOR_TEST);
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevRead));
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDraw));
    f->glDeleteFramebuffers(1, &fbo);
  }

  QImage readTexture(int w, int h) {
    QOpenGLFunctions* f = QOpenGLContext::currentContext()->functions();
    GLint prevRead = 0, prevDraw = 0, prevPbo = 0, prevAlign = 4;
    f->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    f->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    f->glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPbo);
    f->glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlign);
    GLuint fbo = 0;
    f->glGenFramebuffers(1, &fbo);
    f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    f->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    QByteArray buf(w * h * 4, 0);
    f->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevRead));
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDraw));
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(prevPbo));
    f->glPixelStorei(GL_PACK_ALIGNMENT, prevAlign);
    f->glDeleteFramebuffers(1, &fbo);
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {  // image row y = GL row y (the contract: image top row = GL row 0)
      auto* dst = reinterpret_cast<quint32*>(img.scanLine(y));
      const auto* s = reinterpret_cast<const uchar*>(buf.constData()) + static_cast<qsizetype>(y) * w * 4;
      for (int x = 0; x < w; ++x, s += 4) dst[x] = 0xFF000000u | (quint32(s[0]) << 16) | (quint32(s[1]) << 8) | quint32(s[2]);
    }
    return img;
  }

  mutable QMutex mutex_;
  QStringList calls_, violations_;
  QList<Present> presents_;
  QList<QSize> requests_;
  QString failReason_;
  QSize lastRequest_, texSize_;
  unsigned tex_ = 0;
  bool locked_ = false;
  bool lastReleaseGlCurrent_ = false;
  bool textureAliveAtRelease_ = false;
  int textures_ = 0, releases_ = 0;
};

}  // namespace fbtest
