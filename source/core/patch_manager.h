// patch_manager.h - PATCHES-1 (2026-11-12): what the plug-in's Patches page browses.
//
//   <root> = Documents/TimeZero/ms2k/patchmanager
//   <root>/<name>.ms2kbank   one file per USER bank (PresetBank::serialize: up to 128 programs, any count)
//   <root>/library.txt       the registry: which user banks are listed, and the Data Sources (path + program count)
//   <root>/cache/*.ms2kcache the programs of each Data Source as they were last read (the tree shows the counts and
//                            the list opens without the source file; "refresh" reads the file again)
//
// User bank  = a bank you build: create, rename (the file is renamed), remove from the list (the file stays),
//              delete (the file goes), refresh (read the file again), export (PresetBank::toSyx / toMidi).
// Data Source = a .syx / .mid somewhere on disk with any number of programs (a .mid may hold hundreds). Read only:
//              it is parsed when first opened, kept in the cache, never written back.
// Header only and JUCE-free: tested by source/tests/test_patch_manager.cpp. UI thread only.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include "core/preset_bank.h"

namespace MS2000 {

namespace fs = std::filesystem;

// UTF-8 <-> path: a user name in Cyrillic must survive on Windows (std::string paths there are the ANSI page).
inline std::string pathToUtf8(const fs::path& p)
{
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}
inline fs::path pathFromUtf8(const std::string& s)
{
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}

class PatchManager {
public:
    struct UserBank {
        fs::path file;                      // <root>/<name>.ms2kbank (or wherever "Add Bank File" found it)
        std::string name;                   // the file name without the extension
        PresetBank bank;
        long long mtime = 0;                // of the file when read / written: a changed file is read again by load()
    };
    struct Source {
        fs::path path;                      // the .syx / .mid
        std::string name;                   // its file name
        size_t count = 0;                   // programs, from the last read
        bool loaded = false;                // progs holds them
        bool missing = false;               // the file is not there (the cache may still hold the list)
        std::vector<std::vector<uint8_t>> progs;
    };

    explicit PatchManager(fs::path root) : m_root(std::move(root)) {}
    const fs::path& root() const { return m_root; }

    size_t userCount() const { return m_users.size(); }
    UserBank& user(size_t i) { return m_users[i]; }
    const UserBank& user(size_t i) const { return m_users[i]; }
    size_t sourceCount() const { return m_sources.size(); }
    Source& source(size_t i) { return m_sources[i]; }
    const Source& source(size_t i) const { return m_sources[i]; }
    size_t userTotal() const { size_t n = 0; for (const auto& u : m_users) n += u.bank.size(); return n; }
    size_t sourceTotal() const { size_t n = 0; for (const auto& s : m_sources) n += s.count; return n; }
    int indexOfUserFile(const fs::path& f) const
    {
        for (size_t i = 0; i < m_users.size(); ++i) if (sameFile(m_users[i].file, f)) return int(i);
        return -1;
    }
    int indexOfSourcePath(const fs::path& f) const
    {
        for (size_t i = 0; i < m_sources.size(); ++i) if (sameFile(m_sources[i].path, f)) return int(i);
        return -1;
    }

    // ---- names ----
    // A name that is a legal file name everywhere: \ / : * ? " < > | and control characters become '_', no leading
    // or trailing spaces or dots, at most 60 characters (bytes of UTF-8 are never cut in the middle).
    static std::string sanitizeFileName(const std::string& in)
    {
        std::string o;
        for (unsigned char c : in) {
            if (c < 0x20 || c == 0x7F || std::string("\\/:*?\"<>|").find(char(c)) != std::string::npos) o.push_back('_');
            else o.push_back(char(c));
        }
        size_t a = 0, b = o.size();
        while (a < b && (o[a] == ' ' || o[a] == '.')) ++a;
        while (b > a && (o[b - 1] == ' ' || o[b - 1] == '.')) --b;
        o = o.substr(a, b - a);
        if (o.size() > 60) {
            size_t cut = 60;
            while (cut > 0 && (static_cast<unsigned char>(o[cut]) & 0xC0) == 0x80) --cut;   // not inside a UTF-8 sequence
            o = o.substr(0, cut);
            while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
        }
        return o;
    }

    // ---- life ----
    // Reads the registry and the user banks (a missing registry = first run: every *.ms2kbank in the folder is
    // listed). Calling it again re-reads everything from disk (what the page does when it opens).
    bool load(bool* firstRun = nullptr)
    {
        std::error_code ec;
        fs::create_directories(m_root, ec);
        m_users.clear(); m_sources.clear();
        std::vector<uint8_t> raw;
        const bool have = PresetBank::readFile(registryFile(), raw);
        if (firstRun) *firstRun = !have;
        if (have) {
            std::string line;
            bool first = true;
            for (size_t i = 0; i <= raw.size(); ++i) {
                if (i < raw.size() && raw[i] != '\n') { line.push_back(char(raw[i])); continue; }
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (first) { first = false; line.clear(); continue; }                    // the header
                if (line.size() > 2 && line[0] == 'U' && line[1] == '\t') {
                    fs::path p = pathFromUtf8(line.substr(2));
                    if (p.is_relative()) p = m_root / p;
                    loadUserFile(p);
                } else if (line.size() > 4 && line[0] == 'D' && line[1] == '\t') {
                    const size_t t = line.find('\t', 2);
                    if (t != std::string::npos) {
                        Source s;
                        s.path = pathFromUtf8(line.substr(t + 1));
                        s.name = pathToUtf8(s.path.filename());
                        s.count = size_t(std::strtoul(line.substr(2, t - 2).c_str(), nullptr, 10));
                        m_sources.push_back(std::move(s));
                    }
                }
                line.clear();
            }
        } else {
            std::vector<fs::path> found;
            for (fs::directory_iterator it(m_root, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code e2;
                if (it->is_regular_file(e2) && lower(pathToUtf8(it->path().extension())) == ".ms2kbank") found.push_back(it->path());
            }
            std::sort(found.begin(), found.end());
            for (const auto& f : found) loadUserFile(f);
        }
        for (auto& s : m_sources) { std::error_code e; s.missing = !fs::exists(s.path, e); }
        return true;
    }
    bool saveRegistry() const
    {
        std::string t = "MS2K-PATCHMANAGER 1\n";
        for (const auto& u : m_users) {
            const bool inRoot = u.file.parent_path() == m_root;
            t += "U\t" + pathToUtf8(inRoot ? u.file.filename() : u.file) + "\n";
        }
        for (const auto& s : m_sources) t += "D\t" + std::to_string(s.count) + "\t" + pathToUtf8(s.path) + "\n";
        return writeAtomic(registryFile(), std::vector<uint8_t>(t.begin(), t.end()));
    }

    // ---- user banks ----
    // A new empty bank (the name is made unique); returns its index, -1 if the file could not be written.
    int createBank(const std::string& wanted)
    {
        std::error_code ec;
        fs::create_directories(m_root, ec);
        UserBank b;
        b.name = uniqueName(sanitizeFileName(wanted).empty() ? "New Bank" : sanitizeFileName(wanted));
        b.file = m_root / pathFromUtf8(b.name + ".ms2kbank");
        if (!writeAtomic(b.file, b.bank.serialize())) return -1;
        b.mtime = mtimeOf(b.file);
        m_users.push_back(std::move(b));
        saveRegistry();
        return int(m_users.size()) - 1;
    }
    // A new bank holding progs[first .. first+n) (at most 128).
    int createBankFrom(const std::string& wanted, const std::vector<std::vector<uint8_t>>& progs, size_t first, size_t n)
    {
        const int i = createBank(wanted);
        if (i < 0) return -1;
        for (size_t k = first; k < progs.size() && k < first + n; ++k) m_users[size_t(i)].bank.add(progs[k]);
        saveBank(size_t(i));
        return i;
    }
    // A .ms2kbank becomes one bank; a .syx / .mid becomes banks of 128 programs ("Name", "Name 2", ...).
    // Returns the indexes created, empty (and err) if the file holds nothing usable.
    std::vector<int> importFile(const fs::path& file, std::string& err)
    {
        std::vector<int> made;
        std::vector<uint8_t> raw;
        if (!PresetBank::readFile(file, raw)) { err = "Cannot read the file"; return made; }
        const std::string stem = pathToUtf8(file.stem());
        std::vector<std::vector<uint8_t>> progs;
        if (lower(pathToUtf8(file.extension())) == ".ms2kbank") {
            PresetBank pb;
            if (!pb.deserialize(raw)) { err = "Not a bank file"; return made; }
            for (size_t i = 0; i < pb.size(); ++i) progs.push_back(pb.at(i).data);
        } else if (!PresetBank::extractPrograms(raw, progs, err)) return made;
        for (size_t first = 0; first < progs.size(); first += kBankSlots) {
            const int i = createBankFrom(first == 0 ? stem : stem + " " + std::to_string(first / kBankSlots + 1), progs, first, kBankSlots);
            if (i >= 0) made.push_back(i);
        }
        if (made.empty()) err = "Cannot write the bank";
        return made;
    }
    // The single bank of the first plug-in version (MS2000R/userbank.ms2kbank) becomes a bank of the new layout.
    int importLegacy(const fs::path& legacyFile, const std::string& name)
    {
        std::vector<uint8_t> raw;
        PresetBank pb;
        if (!PresetBank::readFile(legacyFile, raw) || !pb.deserialize(raw) || pb.empty()) return -1;
        std::vector<std::vector<uint8_t>> progs;
        for (size_t i = 0; i < pb.size(); ++i) progs.push_back(pb.at(i).data);
        return createBankFrom(name, progs, 0, kBankSlots);
    }
    // Lists a .ms2kbank that lies anywhere (it stays where it is). Returns its index, -1 if it is not a bank.
    int addBankFile(const fs::path& file)
    {
        const int have = indexOfUserFile(file);
        if (have >= 0) return have;
        if (!loadUserFile(file)) return -1;
        saveRegistry();
        return int(m_users.size()) - 1;
    }
    // The file is renamed with the bank. False: the name is empty, taken by another bank or file, or the rename failed.
    bool renameBank(size_t i, const std::string& wanted)
    {
        if (i >= m_users.size()) return false;
        const std::string nn = sanitizeFileName(wanted);
        auto& u = m_users[i];
        if (nn.empty()) return false;
        if (nn == u.name) return true;
        for (size_t j = 0; j < m_users.size(); ++j) if (j != i && lower(m_users[j].name) == lower(nn)) return false;
        const fs::path nf = u.file.parent_path() / pathFromUtf8(nn + ".ms2kbank");
        std::error_code ec;
        if (lower(nn) != lower(u.name) && fs::exists(nf, ec)) return false;
        fs::rename(u.file, nf, ec);
        if (ec) return false;
        u.file = nf; u.name = nn; u.mtime = mtimeOf(nf);
        saveRegistry();
        return true;
    }
    bool removeBank(size_t i)                          // off the list, the file stays
    {
        if (i >= m_users.size()) return false;
        m_users.erase(m_users.begin() + ptrdiff_t(i));
        saveRegistry();
        return true;
    }
    bool deleteBank(size_t i)                          // the file goes too
    {
        if (i >= m_users.size()) return false;
        std::error_code ec;
        fs::remove(m_users[i].file, ec);
        if (ec) return false;
        m_users.erase(m_users.begin() + ptrdiff_t(i));
        saveRegistry();
        return true;
    }
    bool refreshBank(size_t i)                         // the file is read again; false if it cannot be
    {
        if (i >= m_users.size()) return false;
        std::vector<uint8_t> raw;
        PresetBank pb;
        if (!PresetBank::readFile(m_users[i].file, raw) || !pb.deserialize(raw)) return false;
        m_users[i].bank = std::move(pb);
        m_users[i].mtime = mtimeOf(m_users[i].file);
        return true;
    }
    bool saveBank(size_t i)                            // after every change of a bank
    {
        if (i >= m_users.size()) return false;
        if (!writeAtomic(m_users[i].file, m_users[i].bank.serialize())) return false;
        m_users[i].mtime = mtimeOf(m_users[i].file);
        return true;
    }

    // ---- data sources ----
    // Lists a .syx / .mid (reads it once to count its programs). Returns its index (the existing one if it is
    // already listed), -1 and err if it holds no MS2000 programs.
    int addSource(const fs::path& file, std::string& err)
    {
        std::error_code ec;
        fs::path abs = fs::absolute(file, ec);
        if (ec) abs = file;
        const int have = indexOfSourcePath(abs);
        if (have >= 0) return have;
        Source s;
        s.path = abs; s.name = pathToUtf8(abs.filename());
        if (!readSourceFile(s, err)) return -1;
        writeCache(s);
        m_sources.push_back(std::move(s));
        saveRegistry();
        return int(m_sources.size()) - 1;
    }
    // Every .syx / .mid below dir (up to maxFiles listed). Files without MS2000 programs are skipped.
    size_t addSourcesFromFolder(const fs::path& dir, size_t maxFiles = 500)
    {
        std::error_code ec;
        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            const std::string x = lower(pathToUtf8(it->path().extension()));
            if (x == ".syx" || x == ".mid" || x == ".midi") files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        size_t added = 0;
        for (const auto& f : files) {
            if (added >= maxFiles) break;
            const size_t before = m_sources.size();
            std::string err;
            if (addSource(f, err) >= 0 && m_sources.size() > before) ++added;
        }
        return added;
    }
    bool removeSource(size_t i)
    {
        if (i >= m_sources.size()) return false;
        std::error_code ec;
        fs::remove(cacheFile(m_sources[i]), ec);
        m_sources.erase(m_sources.begin() + ptrdiff_t(i));
        saveRegistry();
        return true;
    }
    // Makes the programs of source i available (source(i).progs). From the cache while the file is unchanged, from
    // the file otherwise; when the file is gone the cache still answers.
    bool loadSource(size_t i, std::string& err)
    {
        if (i >= m_sources.size()) return false;
        auto& s = m_sources[i];
        if (s.loaded) return true;
        std::error_code ec;
        if (fs::exists(s.path, ec)) {
            s.missing = false;
            if (readCache(s, false)) { s.loaded = true; return true; }
            if (!readSourceFile(s, err)) return false;
            writeCache(s); saveRegistry();
            return true;
        }
        s.missing = true;
        if (readCache(s, true)) { s.loaded = true; return true; }
        err = "The file is not there";
        return false;
    }
    // Reads the file again (a source that changed on disk).
    bool refreshSource(size_t i, std::string& err)
    {
        if (i >= m_sources.size()) return false;
        auto& s = m_sources[i];
        std::error_code ec;
        s.loaded = false; s.progs.clear();
        if (!fs::exists(s.path, ec)) { s.missing = true; err = "The file is not there"; return false; }
        s.missing = false;
        if (!readSourceFile(s, err)) return false;
        writeCache(s); saveRegistry();
        return true;
    }

    // ---- files ----
    fs::path registryFile() const { return m_root / "library.txt"; }
    fs::path cacheFile(const Source& s) const
    {
        unsigned long long h = 1469598103934665603ULL;                               // FNV-1a of the UTF-8 path
        for (unsigned char c : pathToUtf8(s.path)) { h ^= c; h *= 1099511628211ULL; }
        static const char* hex = "0123456789abcdef";
        std::string n;
        for (int sh = 60; sh >= 0; sh -= 4) n.push_back(hex[(h >> sh) & 15]);
        return m_root / "cache" / pathFromUtf8(n + ".ms2kcache");
    }
    // temp file beside, then moved over: a host killed in the middle never leaves half a file
    static bool writeAtomic(const fs::path& file, const std::vector<uint8_t>& data)
    {
        fs::path tmp = file;
        tmp += ".tmp";
        if (!PresetBank::writeFile(tmp, data)) return false;
        std::error_code ec;
        fs::rename(tmp, file, ec);
        if (ec) {
            std::error_code e2;
            fs::copy_file(tmp, file, fs::copy_options::overwrite_existing, e2);
            fs::remove(tmp, e2);
            return !e2;
        }
        return true;
    }

private:
    static std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }
    static bool sameFile(const fs::path& a, const fs::path& b)
    {
        std::error_code ec;
        if (a == b) return true;
        return fs::exists(a, ec) && fs::exists(b, ec) && fs::equivalent(a, b, ec);
    }
    static long long mtimeOf(const fs::path& f)
    {
        std::error_code ec;
        const auto t = fs::last_write_time(f, ec);
        return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
    }
    static unsigned long long sizeOf(const fs::path& f)
    {
        std::error_code ec;
        const auto n = fs::file_size(f, ec);
        return ec ? 0ULL : static_cast<unsigned long long>(n);
    }
    std::string uniqueName(const std::string& base) const
    {
        auto taken = [&](const std::string& n) {
            for (const auto& u : m_users) if (lower(u.name) == lower(n)) return true;
            std::error_code ec;
            return fs::exists(m_root / pathFromUtf8(n + ".ms2kbank"), ec);
        };
        if (!taken(base)) return base;
        for (int k = 2; k < 10000; ++k) { const std::string n = base + " " + std::to_string(k); if (!taken(n)) return n; }
        return base;
    }
    bool loadUserFile(const fs::path& f)
    {
        std::vector<uint8_t> raw;
        UserBank u;
        if (!PresetBank::readFile(f, raw) || !u.bank.deserialize(raw)) return false;
        u.file = f; u.name = pathToUtf8(f.stem()); u.mtime = mtimeOf(f);
        m_users.push_back(std::move(u));
        return true;
    }
    static bool readSourceFile(Source& s, std::string& err)
    {
        std::vector<uint8_t> raw;
        if (!PresetBank::readFile(s.path, raw)) { err = "Cannot read the file"; return false; }
        std::vector<std::vector<uint8_t>> progs;
        if (!PresetBank::extractPrograms(raw, progs, err)) return false;
        s.progs = std::move(progs);
        s.count = s.progs.size();
        s.loaded = true; s.missing = false;
        return true;
    }
    // "MS2KCAC1", u64 size and i64 mtime of the source when read, u32 count, count x 254 bytes (all big endian)
    void writeCache(const Source& s) const
    {
        std::vector<uint8_t> f = { 'M', 'S', '2', 'K', 'C', 'A', 'C', '1' };
        auto put = [&](unsigned long long v, int bytes) { for (int sh = (bytes - 1) * 8; sh >= 0; sh -= 8) f.push_back(uint8_t(v >> sh)); };
        put(sizeOf(s.path), 8); put(static_cast<unsigned long long>(mtimeOf(s.path)), 8); put(s.progs.size(), 4);
        for (const auto& p : s.progs) f.insert(f.end(), p.begin(), p.end());
        std::error_code ec;
        fs::create_directories(m_root / "cache", ec);
        writeAtomic(cacheFile(s), f);
    }
    bool readCache(Source& s, bool ignoreStamp) const
    {
        std::vector<uint8_t> f;
        if (!PresetBank::readFile(cacheFile(s), f) || f.size() < 28) return false;
        static const char magic[9] = "MS2KCAC1";
        if (!std::equal(magic, magic + 8, f.begin())) return false;
        auto get = [&](size_t at, int bytes) { unsigned long long v = 0; for (int k = 0; k < bytes; ++k) v = (v << 8) | f[at + size_t(k)]; return v; };
        const unsigned long long size = get(8, 8), mt = get(16, 8);
        const size_t n = size_t(get(24, 4));
        if (f.size() != 28 + n * kBankProgramBytes) return false;
        if (!ignoreStamp && (size != sizeOf(s.path) || mt != static_cast<unsigned long long>(mtimeOf(s.path)))) return false;
        s.progs.clear();
        for (size_t i = 0; i < n; ++i)
            s.progs.emplace_back(f.begin() + ptrdiff_t(28 + i * kBankProgramBytes), f.begin() + ptrdiff_t(28 + (i + 1) * kBankProgramBytes));
        s.count = n;
        return true;
    }

    fs::path m_root;
    std::vector<UserBank> m_users;
    std::vector<Source> m_sources;
};

} // namespace MS2000
