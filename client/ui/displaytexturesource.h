#pragma once
// Zero-copy display (ADR 0027): where a GameView gets the GPU texture of its game. Render thread only.
#include <QSize>
class QQuickWindow;
class QSGTexture;

namespace uzume::ui {

class DisplayTextureSource {
 public:
  virtual ~DisplayTextureSource() = default;
  // Render thread, inside updatePaintNode. The texture to draw for this window now (owned by the source, valid until
  // the next call in a later frame) and the size of the image inside it (top-left). nullptr: draw the CPU frame. Several
  // GameViews of one window may call it in the same frame; they get the same texture.
  virtual QSGTexture* texture(QQuickWindow* window, QSize* contentSize) = 0;
  // Render thread: the source failed for good (it already reported it to its producer side). Any thread may read it.
  virtual bool failed() const = 0;
  // Note for implementations: the owner may destroy the source on any thread while the render thread still draws the
  // previous frame (the GameSession is on the GUI thread); release GPU objects on the render thread (e.g. deferred).
};

}  // namespace uzume::ui
