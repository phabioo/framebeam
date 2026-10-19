#include "format_helpers.h"

#include "bytes.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <memory>
#include <mutex>

namespace framebeam::formats {
namespace {

using detail::inRange;

constexpr uint32_t kInvalid = 0xFFFFFFFFu;
constexpr quint64 kMaxMeta = 64u << 20;  // directory/file metadata tables are read into memory

std::string_view view(const QByteArray& b) { return std::string_view(b.constData(), static_cast<size_t>(b.size())); }

bool readAt(QFile& f, quint64 off, quint64 len, QByteArray& out) {
    if (!inRange(static_cast<size_t>(f.size()), off, len)) return false;
    if (!f.seek(static_cast<qint64>(off))) return false;
    out = f.read(static_cast<qint64>(len));
    return static_cast<quint64>(out.size()) == len;
}

std::string utf16leToUtf8(std::string_view s) {
    QString q;
    q.reserve(static_cast<qsizetype>(s.size() / 2));
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        q.append(QChar(static_cast<char16_t>(detail::u8at(s, i) | (detail::u8at(s, i + 1) << 8))));
    const QByteArray u = q.toUtf8();
    return std::string(u.constData(), static_cast<size_t>(u.size()));
}

struct DirEntry {
    uint32_t sibling = kInvalid, childDir = kInvalid, childFile = kInvalid;
    std::string name;
};
struct FileEntry {
    uint32_t sibling = kInvalid;
    uint64_t dataOffset = 0, size = 0;
    std::string name;
};

class Romfs {
public:
    bool open(const QString& path, std::string& err) {
        // The handle only lives for this call: a cached index must not keep the game file locked (Windows).
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            err = "cannot open game file";
            return false;
        }
        QByteArray hdr;
        if (!readAt(f, 0, 0x200, hdr)) {
            err = "game file too small";
            return false;
        }
        quint64 nccBase = 0;
        if (hdr.mid(0x100, 4) == "NCSD") {
            const uint8_t shift = static_cast<uint8_t>(hdr[0x18E]);
            if (shift > 8) {
                err = "NCSD: bad media unit size";
                return false;
            }
            const quint64 unit = quint64(0x200) << shift;
            uint32_t off = 0, size = 0;
            detail::rd32(view(hdr), 0x120, off);
            detail::rd32(view(hdr), 0x124, size);
            if (size == 0) {
                err = "NCSD: partition 0 is empty";
                return false;
            }
            nccBase = off * unit;
            if (!readAt(f, nccBase, 0x200, hdr)) {
                err = "NCSD: partition 0 out of range";
                return false;
            }
        }
        if (hdr.mid(0x100, 4) != "NCCH") {
            err = "not an NCCH/NCSD game file";
            return false;
        }
        if (!(static_cast<uint8_t>(hdr[0x18F]) & 0x4)) {
            err = "encrypted game: FrameBeam needs a decrypted game file";
            return false;
        }
        uint32_t romOff = 0, romSize = 0;
        detail::rd32(view(hdr), 0x1B0, romOff);
        detail::rd32(view(hdr), 0x1B4, romSize);
        if (romSize == 0) {
            err = "game has no RomFS";
            return false;
        }
        const quint64 base = nccBase + quint64(romOff) * 0x200;
        const quint64 size = quint64(romSize) * 0x200;
        if (!inRange(static_cast<size_t>(f.size()), base, size)) {
            err = "RomFS out of range (truncated game file)";
            return false;
        }
        QByteArray ivfc;
        if (!readAt(f, base, 0x60, ivfc) || ivfc.mid(0, 4) != "IVFC") {
            err = "RomFS: bad IVFC header";
            return false;
        }
        uint32_t master = 0, blockLog = 0;
        uint64_t l3Size = 0;
        detail::rd32(view(ivfc), 8, master);
        detail::rd64(view(ivfc), 0x44, l3Size);
        detail::rd32(view(ivfc), 0x4C, blockLog);
        if (blockLog < 1 || blockLog > 30 || master > size) {
            err = "RomFS: bad IVFC parameters";
            return false;
        }
        const quint64 block = quint64(1) << blockLog;
        const quint64 l3Rel = (0x60 + quint64(master) + block - 1) / block * block;
        if (!inRange(static_cast<size_t>(size), l3Rel, l3Size) || l3Size < 0x28) {
            err = "RomFS: level 3 out of range";
            return false;
        }
        l3_ = base + l3Rel;
        l3Size_ = l3Size;
        QByteArray h;
        if (!readAt(f, l3_, 0x28, h)) {
            err = "RomFS: truncated level 3 header";
            return false;
        }
        uint32_t dirOff = 0, dirSize = 0, fileOff = 0, fileSize = 0, dataOff = 0;
        detail::rd32(view(h), 0x0C, dirOff);
        detail::rd32(view(h), 0x10, dirSize);
        detail::rd32(view(h), 0x1C, fileOff);
        detail::rd32(view(h), 0x20, fileSize);
        detail::rd32(view(h), 0x24, dataOff);
        if (dirSize > kMaxMeta || fileSize > kMaxMeta || !inRange(static_cast<size_t>(l3Size), dirOff, dirSize) ||
            !inRange(static_cast<size_t>(l3Size), fileOff, fileSize) || dataOff > l3Size) {
            err = "RomFS: bad metadata table ranges";
            return false;
        }
        if (!readAt(f, l3_ + dirOff, dirSize, dir_) || !readAt(f, l3_ + fileOff, fileSize, file_)) {
            err = "RomFS: truncated metadata tables";
            return false;
        }
        dataOff_ = dataOff;
        return true;
    }

    bool dirAt(uint32_t off, DirEntry& e) const {
        const std::string_view s = view(dir_);
        uint32_t nameLen = 0;
        if (off == kInvalid || !inRange(s.size(), off, 0x18) || !detail::rd32(s, off + 0x14, nameLen)) return false;
        if (!inRange(s.size(), off + 0x18ull, nameLen)) return false;
        detail::rd32(s, off + 4, e.sibling);
        detail::rd32(s, off + 8, e.childDir);
        detail::rd32(s, off + 12, e.childFile);
        e.name = utf16leToUtf8(s.substr(off + 0x18, nameLen));
        return true;
    }
    bool fileAt(uint32_t off, FileEntry& e) const {
        const std::string_view s = view(file_);
        uint32_t nameLen = 0;
        if (off == kInvalid || !inRange(s.size(), off, 0x20) || !detail::rd32(s, off + 0x1C, nameLen)) return false;
        if (!inRange(s.size(), off + 0x20ull, nameLen)) return false;
        detail::rd32(s, off + 4, e.sibling);
        detail::rd64(s, off + 8, e.dataOffset);
        detail::rd64(s, off + 16, e.size);
        e.name = utf16leToUtf8(s.substr(off + 0x20, nameLen));
        return true;
    }

    // Chain walkers; a chain longer than the table could hold is a loop and ends the walk.
    size_t maxDirs() const { return static_cast<size_t>(dir_.size()) / 0x18 + 1; }
    size_t maxFiles() const { return static_cast<size_t>(file_.size()) / 0x20 + 1; }

    bool findDir(uint32_t first, const std::string& name, uint32_t& found) const {
        uint32_t cur = first;
        for (size_t n = 0; n < maxDirs() && cur != kInvalid; ++n) {
            DirEntry e;
            if (!dirAt(cur, e)) return false;
            if (e.name == name) {
                found = cur;
                return true;
            }
            cur = e.sibling;
        }
        return false;
    }
    bool findFile(uint32_t first, const std::string& name, FileEntry& found) const {
        uint32_t cur = first;
        for (size_t n = 0; n < maxFiles() && cur != kInvalid; ++n) {
            FileEntry e;
            if (!fileAt(cur, e)) return false;
            if (e.name == name) {
                found = e;
                return true;
            }
            cur = e.sibling;
        }
        return false;
    }
    // Walk directory components from the root; returns the directory metadata offset.
    bool walkDirs(const std::vector<std::string>& comps, size_t count, uint32_t& dirOff, std::string& err) const {
        dirOff = 0;
        for (size_t i = 0; i < count; ++i) {
            DirEntry cur;
            if (!dirAt(dirOff, cur)) {
                err = "RomFS: corrupt directory table";
                return false;
            }
            uint32_t next = 0;
            if (!findDir(cur.childDir, comps[i], next)) {
                err = "directory not found: " + comps[i];
                return false;
            }
            dirOff = next;
        }
        return true;
    }

    quint64 l3() const { return l3_; }
    quint64 l3Size() const { return l3Size_; }
    quint64 dataOff() const { return dataOff_; }
    const QByteArray& dirTable() const { return dir_; }

    // Listing helper
    bool children(uint32_t dirOff, std::vector<RomfsEntry>& out, std::string& err) const {
        DirEntry d;
        if (!dirAt(dirOff, d)) {
            err = "RomFS: corrupt directory table";
            return false;
        }
        uint32_t cur = d.childDir;
        for (size_t n = 0; n < maxDirs() && cur != kInvalid; ++n) {
            DirEntry e;
            if (!dirAt(cur, e)) {
                err = "RomFS: corrupt directory table";
                return false;
            }
            out.push_back({e.name, true, 0});
            cur = e.sibling;
        }
        cur = d.childFile;
        for (size_t n = 0; n < maxFiles() && cur != kInvalid; ++n) {
            FileEntry e;
            if (!fileAt(cur, e)) {
                err = "RomFS: corrupt file table";
                return false;
            }
            out.push_back({e.name, false, e.size});
            cur = e.sibling;
        }
        return true;
    }

private:
    quint64 l3_ = 0, l3Size_ = 0, dataOff_ = 0;
    QByteArray dir_, file_;
};

std::vector<std::string> splitPath(std::string_view p) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string_view::npos) j = p.size();
        if (j > i) out.emplace_back(p.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

// The parsed index (headers and both metadata tables) of the open game, kept between calls: a plugin that extracts
// hundreds of files would otherwise re-read tables of up to 64 MiB for each of them. One entry; a different game
// path, size or modification time replaces it. No file handle is kept open; all access under one lock.
struct RomfsCache {
    std::mutex mutex;
    QString path;
    qint64 size = -1;
    QDateTime mtime;
    std::unique_ptr<Romfs> fs;
};
RomfsCache& romfsCache() {
    static RomfsCache c;
    return c;
}
// Locks the cache and returns the Romfs of `romPath` (opened now or reused); nullptr + `err` on failure.
Romfs* cachedRomfs(RomfsCache& c, const QString& romPath, std::string& err) {
    const QFileInfo fi(romPath);
    if (c.fs && c.path == romPath && c.size == fi.size() && c.mtime == fi.lastModified()) return c.fs.get();
    c.fs.reset();
    auto fs = std::make_unique<Romfs>();
    if (!fs->open(romPath, err)) return nullptr;
    c.path = romPath;
    c.size = fi.size();
    c.mtime = fi.lastModified();
    c.fs = std::move(fs);
    return c.fs.get();
}

}  // namespace

void romfsCacheClear() {
    RomfsCache& c = romfsCache();
    std::lock_guard<std::mutex> lock(c.mutex);
    c.fs.reset();
    c.path.clear();
}

Result romfsRead(const QString& romPath, std::string_view path, size_t maxOut) {
    Result r;
    RomfsCache& cache = romfsCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    Romfs* fsp = cachedRomfs(cache, romPath, r.error);
    if (!fsp) return r;
    Romfs& fs = *fsp;
    const auto comps = splitPath(path);
    if (comps.empty()) {
        r.error = "path is a directory";
        return r;
    }
    uint32_t dirOff = 0;
    if (!fs.walkDirs(comps, comps.size() - 1, dirOff, r.error)) return r;
    DirEntry d;
    if (!fs.dirAt(dirOff, d)) {
        r.error = "RomFS: corrupt directory table";
        return r;
    }
    FileEntry fe;
    if (!fs.findFile(d.childFile, comps.back(), fe)) {
        r.error = "file not found: " + std::string(path);
        return r;
    }
    if (fe.size > maxOut) {
        r.error = "file exceeds limit";
        return r;
    }
    const quint64 rel = fs.dataOff() + fe.dataOffset;
    if (fe.dataOffset > fs.l3Size() || !inRange(static_cast<size_t>(fs.l3Size()), rel, fe.size)) {
        r.error = "RomFS: file data out of range";
        return r;
    }
    // Opened per read and closed on return, so neither the cache nor a running game locks the ROM on Windows.
    QFile file(romPath);
    if (!file.open(QIODevice::ReadOnly)) {
        r.error = "cannot open game file";
        return r;
    }
    QByteArray data;
    if (!readAt(file, fs.l3() + rel, fe.size, data)) {
        r.error = "RomFS: truncated file data";
        return r;
    }
    r.data.assign(data.constData(), static_cast<size_t>(data.size()));
    r.ok = true;
    return r;
}

std::vector<RomfsEntry> romfsList(const QString& romPath, std::string_view dir, std::string* error) {
    std::vector<RomfsEntry> out;
    std::string err;
    RomfsCache& cache = romfsCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    if (Romfs* fs = cachedRomfs(cache, romPath, err)) {
        const auto comps = splitPath(dir);
        uint32_t dirOff = 0;
        if (fs->walkDirs(comps, comps.size(), dirOff, err)) fs->children(dirOff, out, err);
    }
    if (!err.empty()) out.clear();
    if (error) *error = err;
    return out;
}

}  // namespace framebeam::formats
