// preset_bank.h - BANK-1 (2026-11-11): the user bank of the plugin's preset manager.
//
// A bank is an ordered list of up to 128 programs, each the 254 bytes of MIDI Implementation TABLE 1 (the same
// bytes the machine keeps in its edit buffer and in a PROGRAM DATA DUMP). Nothing here touches the machine: the
// editor takes a program from the edit buffer into the bank, or sends one from the bank into the edit buffer
// through SyxTool::startProgram, exactly as the Library page does.
//
//  - name = bytes 0..11 of the program (12 characters, printable ASCII, space padded);
//  - export .syx = one PROGRAM DATA DUMP (F0 42 3g 58 4C <128 x 254 bytes, 7-bit coded> F7): the only bank message
//    the unit accepts, and it always carries 128 programs. Slots the bank does not fill are taken from `filler`
//    (the editor passes the machine's own program of that slot, so loading the file leaves those slots as they
//    are); without a filler they repeat the first entry under the name "-empty-";
//  - export .mid = a Standard MIDI File (format 0) with that same message as one SysEx event;
//  - import reads .syx and .mid with PROGRAM DUMP (4C), ALL DATA DUMP (50) and CURRENT PROGRAM DUMP (40) messages;
//  - serialize()/deserialize(): the bank's own file (the plugin keeps it between sessions).
// Header only, no JUCE: tested by source/tests/test_preset_bank.cpp.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>
#include "core/syx_tool.h"

namespace MS2000 {

constexpr size_t kBankProgramBytes = 254;
constexpr size_t kBankSlots = 128;
constexpr size_t kBankNameChars = 12;

class PresetBank {
public:
    struct Entry { uint64_t id = 0; std::vector<uint8_t> data; };
    // slot (0..127) -> its 254 bytes; false = no program for that slot
    using Filler = std::function<bool(size_t slot, uint8_t* out254)>;

    size_t size() const { return m_e.size(); }
    bool empty() const { return m_e.empty(); }
    bool full() const { return m_e.size() >= kBankSlots; }
    const Entry& at(size_t i) const { return m_e[i]; }
    int indexOfId(uint64_t id) const
    {
        for (size_t i = 0; i < m_e.size(); ++i) if (m_e[i].id == id) return int(i);
        return -1;
    }
    void clear() { m_e.clear(); }

    // ---- names ----
    // Printable ASCII (0x20..0x7E) only, cut at 12 characters, padded with spaces to 12.
    static std::string sanitizeName(const std::string& s)
    {
        std::string o;
        for (char c : s) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u >= 0x20 && u < 0x7F) o.push_back(c);
            if (o.size() == kBankNameChars) break;
        }
        o.resize(kBankNameChars, ' ');
        return o;
    }
    // The name as shown: the 12 bytes without the trailing spaces.
    static std::string nameOf(const uint8_t* prog)
    {
        std::string n;
        for (size_t i = 0; i < kBankNameChars; ++i) n.push_back((prog[i] >= 0x20 && prog[i] < 0x7F) ? char(prog[i]) : ' ');
        while (!n.empty() && n.back() == ' ') n.pop_back();
        return n;
    }
    std::string name(size_t i) const { return nameOf(m_e[i].data.data()); }
    // The program with its name replaced (the bytes of a program are untouched otherwise).
    static std::vector<uint8_t> withName(std::vector<uint8_t> prog, const std::string& name)
    {
        const std::string s = sanitizeName(name);
        for (size_t i = 0; i < kBankNameChars; ++i) prog[i] = uint8_t(s[i]);
        return prog;
    }

    // ---- editing ----
    // Appends a program; returns its index, -1 if the bank is full or the data is not 254 bytes.
    int add(const std::vector<uint8_t>& prog)
    {
        if (full() || prog.size() != kBankProgramBytes) return -1;
        Entry e; e.id = ++m_lastId; e.data = prog;
        m_e.push_back(std::move(e));
        return int(m_e.size()) - 1;
    }
    // The stored program of entry i is replaced (id and position stay).
    bool replace(size_t i, const std::vector<uint8_t>& prog)
    {
        if (i >= m_e.size() || prog.size() != kBankProgramBytes) return false;
        m_e[i].data = prog;
        return true;
    }
    bool remove(size_t i)
    {
        if (i >= m_e.size()) return false;
        m_e.erase(m_e.begin() + ptrdiff_t(i));
        return true;
    }
    bool rename(size_t i, const std::string& name)
    {
        if (i >= m_e.size()) return false;
        m_e[i].data = withName(std::move(m_e[i].data), name);
        return true;
    }
    // Drag and drop: entry `from` is dropped into the gap `gap` (0 = above the first entry, size() = below the
    // last). Returns the entry's new index, or -1 if nothing moved.
    int moveToGap(size_t from, size_t gap)
    {
        if (from >= m_e.size() || gap > m_e.size()) return -1;
        const size_t to = gap > from ? gap - 1 : gap;
        if (to == from) return -1;
        Entry e = std::move(m_e[from]);
        m_e.erase(m_e.begin() + ptrdiff_t(from));
        m_e.insert(m_e.begin() + ptrdiff_t(to), std::move(e));
        return int(to);
    }
    // Appends what a file holds (as many as fit); returns how many were added.
    size_t addAll(const std::vector<std::vector<uint8_t>>& progs)
    {
        size_t n = 0;
        for (const auto& p : progs) { if (add(p) < 0) break; ++n; }
        return n;
    }

    // ---- export ----
    // The 128 programs of the file: the bank's entries in order, then the filler for the rest. Empty bank -> {}.
    std::vector<uint8_t> slots128(const Filler& filler) const
    {
        std::vector<uint8_t> all;
        if (m_e.empty()) return all;
        all.reserve(kBankSlots * kBankProgramBytes);
        for (size_t s = 0; s < kBankSlots; ++s) {
            if (s < m_e.size()) { all.insert(all.end(), m_e[s].data.begin(), m_e[s].data.end()); continue; }
            uint8_t buf[kBankProgramBytes];
            if (filler && filler(s, buf)) { all.insert(all.end(), buf, buf + kBankProgramBytes); continue; }
            const auto p = withName(m_e[0].data, "-empty-");
            all.insert(all.end(), p.begin(), p.end());
        }
        return all;
    }
    // PROGRAM DATA DUMP, 128 programs (MIDI channel nibble 0: the loader puts the machine's own channel in).
    std::vector<uint8_t> toSyx(const Filler& filler = {}) const
    {
        const auto all = slots128(filler);
        if (all.empty()) return {};
        std::vector<uint8_t> m = { 0xF0, 0x42, 0x30, 0x58, 0x4C };
        const auto enc = SyxTool::encode7(all);
        m.insert(m.end(), enc.begin(), enc.end());
        m.push_back(0xF7);
        return m;
    }
    // Standard MIDI File, format 0, one track: the track name, the PROGRAM DATA DUMP as one SysEx event, the end.
    std::vector<uint8_t> toMidi(const std::string& title, const Filler& filler = {}) const
    {
        const auto syx = toSyx(filler);
        if (syx.empty()) return {};
        std::vector<uint8_t> trk;
        const std::string t = title.empty() ? std::string("MS2000R user bank") : title;
        trk.push_back(0x00); trk.push_back(0xFF); trk.push_back(0x03); putVlq(trk, t.size());
        trk.insert(trk.end(), t.begin(), t.end());
        trk.push_back(0x00); trk.push_back(0xF0); putVlq(trk, syx.size() - 1);          // length of what follows the F0
        trk.insert(trk.end(), syx.begin() + 1, syx.end());
        trk.push_back(0x00); trk.push_back(0xFF); trk.push_back(0x2F); trk.push_back(0x00);
        std::vector<uint8_t> f = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k' };
        put32(f, uint32_t(trk.size()));
        f.insert(f.end(), trk.begin(), trk.end());
        return f;
    }

    // ---- import ----
    // The programs a .syx or .mid holds, in file order. False (and an error text) if it has none.
    static bool extractPrograms(const std::vector<uint8_t>& file, std::vector<std::vector<uint8_t>>& out, std::string& err)
    {
        out.clear();
        std::vector<uint8_t> syx;
        if (file.size() >= 14 && file[0] == 'M' && file[1] == 'T' && file[2] == 'h' && file[3] == 'd') {
            if (!midiSysex(file, syx)) { err = "Not a readable MIDI file"; return false; }
        } else syx = file;
        for (size_t i = 0; i + 6 < syx.size(); ++i) {
            if (syx[i] != 0xF0 || syx[i + 1] != 0x42 || (syx[i + 2] & 0xF0) != 0x30 || syx[i + 3] != 0x58) continue;
            size_t j = i + 5; while (j < syx.size() && syx[j] != 0xF7) ++j;
            if (j >= syx.size()) break;
            const uint8_t fn = syx[i + 4];
            const size_t count = fn == 0x40 ? 1 : (fn == 0x4C || fn == 0x50) ? kBankSlots : 0;
            const auto d = SyxTool::decode7(std::vector<uint8_t>(syx.begin() + ptrdiff_t(i) + 5, syx.begin() + ptrdiff_t(j)));
            for (size_t k = 0; k < count && (k + 1) * kBankProgramBytes <= d.size(); ++k)
                out.emplace_back(d.begin() + ptrdiff_t(k * kBankProgramBytes), d.begin() + ptrdiff_t((k + 1) * kBankProgramBytes));
            i = j;
        }
        if (out.empty()) { err = "No MS2000 programs in the file"; return false; }
        return true;
    }
    // File helpers on std::filesystem paths: on Windows a path with non-ASCII characters (a user name in Cyrillic)
    // works through the wide API, which fopen(narrow string) would not.
    static bool readFile(const std::filesystem::path& path, std::vector<uint8_t>& f)
    {
        f.clear();
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        f.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return !in.bad();
    }
    static bool writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& f)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(f.data()), std::streamsize(f.size()));
        out.flush();
        return bool(out);
    }

    // ---- the clipboard text of one program ----
    // "MS2000R-PATCH-1:" and the 254 bytes as 508 hex digits. Text, so it goes through the system clipboard and
    // between plug-in instances (and a text editor) unchanged.
    static std::string toClipboardText(const std::vector<uint8_t>& prog)
    {
        static const char* hex = "0123456789ABCDEF";
        std::string t = "MS2000R-PATCH-1:";
        for (uint8_t b : prog) { t.push_back(hex[b >> 4]); t.push_back(hex[b & 15]); }
        return t;
    }
    static bool fromClipboardText(const std::string& text, std::vector<uint8_t>& prog)
    {
        size_t a = 0, b = text.size();
        while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
        static const std::string tag = "MS2000R-PATCH-1:";
        if (b - a != tag.size() + kBankProgramBytes * 2 || text.compare(a, tag.size(), tag) != 0) return false;
        std::vector<uint8_t> p;
        for (size_t i = a + tag.size(); i < b; i += 2) {
            auto nib = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
            const int h = nib(text[i]), l = nib(text[i + 1]);
            if (h < 0 || l < 0) return false;
            p.push_back(uint8_t(h * 16 + l));
        }
        prog = std::move(p);
        return true;
    }

    // ---- the bank's own file (kept between sessions) ----
    // "MS2KBNK1", u16 count (big endian), count x 254 bytes.
    std::vector<uint8_t> serialize() const
    {
        std::vector<uint8_t> f = { 'M', 'S', '2', 'K', 'B', 'N', 'K', '1', uint8_t(m_e.size() >> 8), uint8_t(m_e.size() & 0xFF) };
        for (const auto& e : m_e) f.insert(f.end(), e.data.begin(), e.data.end());
        return f;
    }
    bool deserialize(const std::vector<uint8_t>& f)
    {
        static const char magic[9] = "MS2KBNK1";
        if (f.size() < 10 || !std::equal(magic, magic + 8, f.begin())) return false;
        const size_t n = (size_t(f[8]) << 8) | f[9];
        if (n > kBankSlots || f.size() != 10 + n * kBankProgramBytes) return false;
        m_e.clear();
        for (size_t i = 0; i < n; ++i)
            add(std::vector<uint8_t>(f.begin() + ptrdiff_t(10 + i * kBankProgramBytes), f.begin() + ptrdiff_t(10 + (i + 1) * kBankProgramBytes)));
        return true;
    }

private:
    static void putVlq(std::vector<uint8_t>& o, size_t v)
    {
        uint8_t b[5]; int n = 0;
        b[n++] = uint8_t(v & 0x7F);
        while (v >>= 7) b[n++] = uint8_t(0x80 | (v & 0x7F));
        while (n--) o.push_back(b[n]);
    }
    static void put32(std::vector<uint8_t>& o, uint32_t v) { for (int s = 24; s >= 0; s -= 8) o.push_back(uint8_t(v >> s)); }
    // The SysEx of every track of an SMF, back to back as raw MIDI bytes (F0 data.. F7). Events of other kinds
    // are skipped (running status included).
    static bool midiSysex(const std::vector<uint8_t>& f, std::vector<uint8_t>& out)
    {
        size_t p = 8 + ((size_t(f[4]) << 24) | (size_t(f[5]) << 16) | (size_t(f[6]) << 8) | f[7]);   // after the header chunk
        while (p + 8 <= f.size()) {
            const size_t len = (size_t(f[p + 4]) << 24) | (size_t(f[p + 5]) << 16) | (size_t(f[p + 6]) << 8) | f[p + 7];
            const bool trk = f[p] == 'M' && f[p + 1] == 'T' && f[p + 2] == 'r' && f[p + 3] == 'k';
            size_t q = p + 8; const size_t end = (q + len < f.size()) ? q + len : f.size();   // no std::min: windows.h defines a min macro
            p = end;
            if (!trk) continue;
            uint8_t run = 0;
            auto vlq = [&](size_t& v) { v = 0; for (int k = 0; k < 4 && q < end; ++k) { const uint8_t b = f[q++]; v = (v << 7) | (b & 0x7F); if (!(b & 0x80)) return true; } return false; };
            while (q < end) {
                size_t dt; if (!vlq(dt) || q >= end) return false;
                uint8_t st = f[q];
                if (st == 0xFF) { if (q + 1 >= end) return false; q += 2; size_t n; if (!vlq(n) || q + n > end) return false; q += n; continue; }
                if (st == 0xF0 || st == 0xF7) {
                    ++q; size_t n; if (!vlq(n) || q + n > end) return false;
                    if (st == 0xF0) out.push_back(0xF0);
                    out.insert(out.end(), f.begin() + ptrdiff_t(q), f.begin() + ptrdiff_t(q + n));
                    q += n; continue;
                }
                if (st & 0x80) { run = st; ++q; } else if (!run) return false; else st = run;
                const uint8_t hi = st & 0xF0;
                q += (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            }
        }
        return true;
    }

    std::vector<Entry> m_e;
    uint64_t m_lastId = 0;
};

} // namespace MS2000
