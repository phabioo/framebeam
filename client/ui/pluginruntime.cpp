#include "pluginruntime.h"

#include <QImage>
#include <cstring>
#include <QMetaObject>

#include "gamesession.h"
#include "screenlayout.h"

namespace uzume::ui {

namespace {
std::vector<emu::WatchRange> toEmu(const std::vector<plugins::WatchRange>& in) {
  std::vector<emu::WatchRange> out;
  out.reserve(in.size());
  for (const auto& r : in) out.push_back({r.addr, r.len});
  return out;
}
}  // namespace

MemoryWatchAdapter::MemoryWatchAdapter(std::shared_ptr<emu::MemoryWatchSet> set) : m_set(std::move(set)) {}

MemoryWatchAdapter::~MemoryWatchAdapter() { clear(); }

void MemoryWatchAdapter::bind(std::shared_ptr<emu::MemoryWatchSet> set) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_set && m_set != set)
    for (const auto& kv : m_owners) m_set->clear(kv.first);
  m_set = std::move(set);
  if (m_set)
    for (const auto& kv : m_owners) m_set->setRanges(kv.first, toEmu(kv.second));
}

void MemoryWatchAdapter::clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_set)
    for (const auto& kv : m_owners) m_set->clear(kv.first);
  m_owners.clear();
}

void MemoryWatchAdapter::setWatches(const std::string& owner, std::vector<plugins::WatchRange> ranges) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (ranges.empty()) {
    m_owners.erase(owner);
    if (m_set) m_set->clear(owner);
    return;
  }
  if (m_set) m_set->setRanges(owner, toEmu(ranges));
  m_owners[owner] = std::move(ranges);
}

std::shared_ptr<const emu::MemorySnapshot> MemoryWatchAdapter::latest() const {
  std::shared_ptr<emu::MemoryWatchSet> set;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    set = m_set;
  }
  return set ? set->latest() : nullptr;
}

bool MemoryWatchAdapter::read(uint64_t addr, void* out, size_t len) const {
  const auto snap = latest();
  return snap && snap->read(addr, out, len);
}

std::optional<uint64_t> MemoryWatchAdapter::frame() const {
  const auto snap = latest();
  if (!snap) return std::nullopt;
  return snap->frame();
}

uint64_t MemoryWatchAdapter::generation() const {
  std::shared_ptr<emu::MemoryWatchSet> set;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    set = m_set;
  }
  return set ? set->generation() : 0;
}

VideoCaptureAdapter::VideoCaptureAdapter(GameSession* session, QObject* parent) : QObject(parent), m_session(session) {
  if (!session) return;
  connect(session, &GameSession::stateChanged, this, &VideoCaptureAdapter::refresh);
  connect(session, &GameSession::frameChanged, this, &VideoCaptureAdapter::refresh);
  refresh();
}

VideoCaptureAdapter::~VideoCaptureAdapter() = default;

std::vector<plugins::ScreenInfo> VideoCaptureAdapter::screensFor(const GameSession& session) {
  std::vector<plugins::ScreenInfo> out;
  const emu::DisplayProfile& profile = session.displayProfile();
  const int n = static_cast<int>(profile.screens.size());
  QSize frame = session.sourceFrameSize();
  if (frame.isEmpty()) frame = session.frame().size();
  if (!session.isActive() || n == 0 || frame.isEmpty()) return out;
  for (int i = 0; i < n; ++i) {
    const QRectF r = screenSourceRect(profile, QSizeF(frame), i).toAlignedRect();
    if (r.isEmpty()) continue;
    QString name = n == 1 ? QStringLiteral("main") : profile.screens.at(i).id;
    if (name.isEmpty()) name = i == 0 ? QStringLiteral("top") : QStringLiteral("bottom");
    out.push_back({name.toStdString(), static_cast<int>(r.x()), static_cast<int>(r.y()), static_cast<int>(r.width()), static_cast<int>(r.height())});
  }
  return out;
}

void VideoCaptureAdapter::refresh() {
  if (!m_session) return;
  std::vector<plugins::ScreenInfo> now = screensFor(*m_session);
  std::lock_guard<std::mutex> lock(m_mutex);
  m_screens = std::move(now);
}

std::vector<plugins::ScreenInfo> VideoCaptureAdapter::screens() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_screens;
}

void VideoCaptureAdapter::captureFrame(std::function<void(plugins::RawFrame)> done) {
  QMetaObject::invokeMethod(
      this,
      [this, done]() {
        if (!m_session) {
          plugins::RawFrame f;
          f.error = "no_frame";
          done(std::move(f));
          return;
        }
        m_session->requestNativeFrame([done](const QImage& img, const QString& error) {
          plugins::RawFrame f;
          if (!error.isEmpty() || img.isNull()) {
            f.error = error.isEmpty() ? "no_frame" : error.toStdString();
          } else {
            const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);  // opaque alpha for the core's RGB32 frames
            f.w = rgba.width();
            f.h = rgba.height();
            f.rgba.resize(static_cast<size_t>(f.w) * f.h * 4);
            for (int y = 0; y < f.h; ++y) std::memcpy(&f.rgba[static_cast<size_t>(y) * f.w * 4], rgba.constScanLine(y), static_cast<size_t>(f.w) * 4);
          }
          done(std::move(f));
        });
      },
      Qt::QueuedConnection);
}

}  // namespace uzume::ui
