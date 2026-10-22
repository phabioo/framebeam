#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <map>
#include <mutex>

#include "manifest.h"
#include "plugin_memory.h"

namespace uzume::plugins::testutil {

struct PluginSpec {
  QString id = QStringLiteral("net.test.plugin");
  QString name;
  QString version = QStringLiteral("1.0.0");
  QStringList permissions;
  QJsonObject requires_;
  QStringList provides;
  QJsonArray games;
  QJsonObject ui;   // manifest field "ui" (needs the permission "ui")
  QString minPlayer;  // manifest field "min_player"
  QString lua;
  QMap<QString, QByteArray> files;  // extra files below the plugin folder
};

inline QString writePlugin(const QString& parent, const PluginSpec& s) {
  const QString dir = QDir(parent).filePath(s.id);
  QDir().mkpath(dir);
  QJsonObject m;
  m["id"] = s.id;
  m["name"] = s.name.isEmpty() ? s.id : s.name;
  m["version"] = s.version;
  m["api"] = 1;
  m["type"] = "script";
  m["entry"] = "main.lua";
  QJsonArray perms;
  for (const QString& p : s.permissions) perms.append(p);
  m["permissions"] = perms;
  if (!s.requires_.isEmpty()) m["requires"] = s.requires_;
  if (!s.provides.isEmpty()) m["provides"] = QJsonArray::fromStringList(s.provides);
  if (!s.games.isEmpty()) m["games"] = s.games;
  if (!s.ui.isEmpty()) m["ui"] = s.ui;
  if (!s.minPlayer.isEmpty()) m["min_player"] = s.minPlayer;
  QFile mf(dir + "/manifest.json");
  mf.open(QIODevice::WriteOnly);
  mf.write(QJsonDocument(m).toJson());
  mf.close();
  QFile lf(dir + "/main.lua");
  lf.open(QIODevice::WriteOnly);
  lf.write(s.lua.toUtf8());
  lf.close();
  for (auto it = s.files.begin(); it != s.files.end(); ++it) {
    QDir().mkpath(QFileInfo(dir + "/" + it.key()).absolutePath());
    QFile f(dir + "/" + it.key());
    f.open(QIODevice::WriteOnly);
    f.write(it.value());
  }
  return dir;
}

inline PluginInfo infoFor(const QString& dir, const QString& cacheRoot) {
  const ManifestResult r = loadManifest(dir);
  PluginInfo i;
  i.manifest = r.manifest;
  i.path = dir;
  i.cacheDir = cacheRoot + "/" + r.manifest.id;
  i.enabled = true;
  return i;
}

// Emulated RAM plus the snapshot semantics of the real provider: setWatches registers ranges, snapshot() copies the
// registered ranges of all owners into the readable snapshot and bumps frame and generation.
class FakeMemoryProvider : public MemoryProvider {
 public:
  void setRam(uint64_t addr, const std::vector<uint8_t>& bytes) {
    std::lock_guard<std::mutex> l(m_);
    for (size_t i = 0; i < bytes.size(); ++i) ram_[addr + i] = bytes[i];
  }
  void setU32(uint64_t addr, uint32_t v) {
    setRam(addr, {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)});
  }
  void snapshot() {
    std::lock_guard<std::mutex> l(m_);
    snap_.clear();
    for (const auto& w : watches_)
      for (const WatchRange& r : w.second)
        for (uint64_t a = r.addr; a < r.addr + r.len; ++a) {
          auto it = ram_.find(a);
          if (it != ram_.end()) snap_[a] = it->second;
        }
    frame_ = frame_ ? *frame_ + 1 : 1;
    ++gen_;
  }
  std::vector<WatchRange> watchesOf(const std::string& owner) const {
    std::lock_guard<std::mutex> l(m_);
    auto it = watches_.find(owner);
    return it == watches_.end() ? std::vector<WatchRange>() : it->second;
  }
  void setWatches(const std::string& owner, std::vector<WatchRange> ranges) override {
    std::lock_guard<std::mutex> l(m_);
    watches_[owner] = std::move(ranges);
  }
  bool read(uint64_t addr, void* out, size_t len) const override {
    std::lock_guard<std::mutex> l(m_);
    auto* p = static_cast<uint8_t*>(out);
    for (size_t i = 0; i < len; ++i) {
      auto it = snap_.find(addr + i);
      if (it == snap_.end()) return false;
      p[i] = it->second;
    }
    return true;
  }
  std::optional<uint64_t> frame() const override {
    std::lock_guard<std::mutex> l(m_);
    return frame_;
  }
  uint64_t generation() const override {
    std::lock_guard<std::mutex> l(m_);
    return gen_;
  }

 private:
  mutable std::mutex m_;
  std::map<uint64_t, uint8_t> ram_, snap_;
  std::map<std::string, std::vector<WatchRange>> watches_;
  std::optional<uint64_t> frame_;
  uint64_t gen_ = 0;
};

}  // namespace uzume::plugins::testutil
