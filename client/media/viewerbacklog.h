#pragma once

#include <QByteArray>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace uzume {

// True when an Annex B access unit carries a parameter set (SPS) or an IDR slice, i.e. decoding can restart there.
inline bool startsKeyframe(const QByteArray& d) {
  const auto* p = reinterpret_cast<const uint8_t*>(d.constData());
  const size_t n = static_cast<size_t>(d.size());
  for (size_t i = 0; i + 4 < n; ++i) {
    if (p[i] != 0 || p[i + 1] != 0) continue;
    size_t h = 0;
    if (p[i + 2] == 1) {
      h = i + 3;
    } else if (p[i + 2] == 0 && p[i + 3] == 1) {
      h = i + 4;
    } else {
      continue;
    }
    if (h < n) {
      const int type = p[h] & 0x1f;
      if (type == 5 || type == 7) return true;
    }
  }
  return false;
}

// Bounded queue of encoded video access units between the network threads and the decoder (PN-6). When the decoder (or the UI
// behind it) falls behind, the whole backlog is dropped and everything up to the next keyframe is discarded, because P frames
// without their references only produce artifacts; the caller asks for a keyframe. Not thread-safe: the owner holds a lock.
class VideoBacklog {
 public:
  explicit VideoBacklog(size_t maxQueued) : max_(maxQueued) {}

  struct PushResult {
    size_t dropped = 0;        // access units discarded by this call (including `data` itself)
    bool wantKeyframe = false;  // the stream needs a keyframe to continue
  };

  PushResult push(QByteArray data, bool key) {
    PushResult r;
    if (queue_.size() >= max_) {
      r.dropped += queue_.size();
      queue_.clear();
      needKey_ = true;
    }
    if (needKey_ && !key) {
      ++r.dropped;
      r.wantKeyframe = true;
      return r;
    }
    needKey_ = false;
    queue_.push_back(std::move(data));
    return r;
  }

  bool empty() const { return queue_.empty(); }
  size_t size() const { return queue_.size(); }
  bool waitingForKeyframe() const { return needKey_; }
  QByteArray pop() {
    QByteArray b = std::move(queue_.front());
    queue_.pop_front();
    return b;
  }

 private:
  size_t max_;
  std::deque<QByteArray> queue_;
  bool needKey_ = false;
};

}  // namespace uzume
