// test_preset_bank.cpp - BANK-1: the preset manager's bank (core/preset_bank.h), no machine needed.
#include <cstdio>
#include <cstdlib>
#include <string>
#include "core/preset_bank.h"

using namespace MS2000;
static const char* kTmp = "ms2k_bank_test.syx";      // in the current folder: the same on Windows and Linux
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static std::vector<uint8_t> prog(const std::string& name, uint8_t seed)
{
    std::vector<uint8_t> p(kBankProgramBytes);
    for (size_t i = 0; i < p.size(); ++i) p[i] = uint8_t((seed * 31u + i * 7u) & 0xFF);   // high bits set too
    const std::string s = PresetBank::sanitizeName(name);
    for (size_t i = 0; i < 12; ++i) p[i] = uint8_t(s[i]);
    return p;
}

int main()
{
    // names
    CHECK(PresetBank::sanitizeName("Abc") == "Abc         ");
    CHECK(PresetBank::sanitizeName("0123456789ABCDEF") == "0123456789AB");
    CHECK(PresetBank::sanitizeName(std::string("A\x01") + "B\xC3\xA9" + "C") == "ABC         ");   // control + non-ASCII dropped
    CHECK(PresetBank::nameOf(prog("Psy Bass", 1).data()) == "Psy Bass");

    // add / replace / remove / rename
    PresetBank b;
    CHECK(b.add(prog("One", 1)) == 0 && b.add(prog("Two", 2)) == 1 && b.add(prog("Three", 3)) == 2 && b.add(prog("Four", 4)) == 3);
    CHECK(b.add(std::vector<uint8_t>(10)) == -1);
    CHECK(b.size() == 4 && b.name(2) == "Three");
    const auto idTwo = b.at(1).id;
    CHECK(b.rename(1, "Renamed Two!!") && b.name(1) == "Renamed Two!");     // cut at 12
    CHECK(b.at(1).data[12] == prog("x", 2)[12] && b.at(1).data[253] == prog("x", 2)[253]);   // only the name changed
    CHECK(b.replace(1, prog("Live", 9)) && b.name(1) == "Live" && b.at(1).id == idTwo);
    CHECK(!b.replace(9, prog("x", 1)) && !b.rename(9, "x") && !b.remove(9));

    // drag and drop: gaps 0..size
    // order now: One Live Three Four
    CHECK(b.moveToGap(3, 0) == 0);                                             // Four to the top
    CHECK(b.name(0) == "Four" && b.name(1) == "One" && b.name(2) == "Live" && b.name(3) == "Three");
    CHECK(b.moveToGap(0, 4) == 3);                                             // Four to the bottom
    CHECK(b.name(0) == "One" && b.name(3) == "Four");
    CHECK(b.moveToGap(1, 1) == -1 && b.moveToGap(1, 2) == -1);                 // dropped on itself: no move
    CHECK(b.moveToGap(0, 3) == 2);                                             // One between Three and Four
    CHECK(b.name(0) == "Live" && b.name(1) == "Three" && b.name(2) == "One" && b.name(3) == "Four");
    CHECK(b.indexOfId(idTwo) == 0);                                            // the id follows the entry
    CHECK(b.remove(0) && b.size() == 3 && b.indexOfId(idTwo) == -1);

    // syx export: one PROGRAM DUMP, 128 slots, read back by the existing Library parser
    const auto syx = b.toSyx();
    CHECK(syx.size() == 5 + 37157 + 1);
    CHECK(syx[0] == 0xF0 && syx[1] == 0x42 && syx[2] == 0x30 && syx[3] == 0x58 && syx[4] == 0x4C && syx.back() == 0xF7);
    for (size_t i = 1; i + 1 < syx.size(); ++i) if (i >= 5 && (syx[i] & 0x80)) { CHECK(false); break; }
    CHECK(PresetBank::writeFile(kTmp, syx));
    std::vector<SyxTool::Program> lib; std::string err;
    CHECK(SyxTool::parsePrograms(kTmp, lib, err) && lib.size() == 128);
    CHECK(lib[0].name == "Three" && lib[1].name == "One" && lib[2].name == "Four");
    CHECK(lib[0].data == b.at(0).data && lib[2].data == b.at(2).data);
    std::remove(kTmp);
    CHECK(lib[3].name == "-empty-" && lib[127].name == "-empty-");              // default filler
    // with a filler: the machine's own programs fill the rest
    const auto syxF = b.toSyx([](size_t slot, uint8_t* out) { const auto p = prog("Mach" + std::to_string(slot), uint8_t(slot)); std::copy(p.begin(), p.end(), out); return true; });
    std::vector<std::vector<uint8_t>> got;
    CHECK(PresetBank::extractPrograms(syxF, got, err) && got.size() == 128);
    CHECK(PresetBank::nameOf(got[3].data()) == "Mach3" && PresetBank::nameOf(got[127].data()) == "Mach127");
    CHECK(got[0] == b.at(0).data);
    // a full bank needs no filler at all
    PresetBank full; for (int i = 0; i < 128; ++i) CHECK(full.add(prog("P" + std::to_string(i), uint8_t(i))) == i);
    CHECK(full.add(prog("no room", 1)) == -1 && full.full());
    CHECK(PresetBank::extractPrograms(full.toSyx(), got, err) && got.size() == 128 && PresetBank::nameOf(got[127].data()) == "P127");
    CHECK(PresetBank().toSyx().empty() && PresetBank().toMidi("x").empty());

    // mid export and import
    const auto mid = b.toMidi("My bank");
    CHECK(mid.size() > 37000 && std::string(mid.begin(), mid.begin() + 4) == "MThd" && std::string(mid.begin() + 14, mid.begin() + 18) == "MTrk");
    CHECK(mid[8] == 0 && mid[9] == 0 && mid[10] == 0 && mid[11] == 1);          // format 0, one track
    CHECK(mid[mid.size() - 3] == 0xFF && mid[mid.size() - 2] == 0x2F && mid[mid.size() - 1] == 0x00);
    const size_t trkLen = (size_t(mid[18]) << 24) | (size_t(mid[19]) << 16) | (size_t(mid[20]) << 8) | mid[21];
    CHECK(trkLen == mid.size() - 22);
    CHECK(PresetBank::extractPrograms(mid, got, err) && got.size() == 128);
    CHECK(got[0] == b.at(0).data && got[1] == b.at(1).data && got[2] == b.at(2).data);
    // a hand-made SMF with a note, running status and a meta before the SysEx is still read
    {
        std::vector<uint8_t> t = { 0, 0xFF, 0x58, 4, 4, 2, 24, 8,  0, 0x90, 60, 100,  0x10, 60, 0,  0, 0xC0, 5 };
        const auto one = std::vector<uint8_t>{ 0xF0, 0x42, 0x30, 0x58, 0x40 };
        auto m = one; const auto enc = SyxTool::encode7(prog("Single", 5)); m.insert(m.end(), enc.begin(), enc.end()); m.push_back(0xF7);
        const size_t sl = m.size() - 1;                                       // 296: a two-byte VLQ
        t.push_back(0); t.push_back(0xF0); t.push_back(uint8_t(0x80 | (sl >> 7))); t.push_back(uint8_t(sl & 0x7F));
        t.insert(t.end(), m.begin() + 1, m.end());
        t.push_back(0); t.push_back(0xFF); t.push_back(0x2F); t.push_back(0);
        std::vector<uint8_t> f = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k',
                                   0, 0, uint8_t(t.size() >> 8), uint8_t(t.size() & 0xFF) };
        f.insert(f.end(), t.begin(), t.end());
        CHECK(PresetBank::extractPrograms(f, got, err) && got.size() == 1 && PresetBank::nameOf(got[0].data()) == "Single");
    }
    CHECK(!PresetBank::extractPrograms({ 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 1, 0xE0 }, got, err));
    CHECK(!PresetBank::extractPrograms({ 1, 2, 3, 4, 5, 6, 7, 8 }, got, err));

    // a single CURRENT PROGRAM DUMP (the Library's click path) imports as one
    {
        std::vector<uint8_t> m = { 0xF0, 0x42, 0x30, 0x58, 0x40 }; const auto enc = SyxTool::encode7(prog("Cur", 7));
        m.insert(m.end(), enc.begin(), enc.end()); m.push_back(0xF7);
        CHECK(PresetBank::extractPrograms(m, got, err) && got.size() == 1 && got[0] == prog("Cur", 7));
        PresetBank c; CHECK(c.addAll(got) == 1 && c.name(0) == "Cur");
    }

    // the clipboard text of one program
    {
        const auto p = prog("Clip Pad", 11);
        const auto t = PresetBank::toClipboardText(p);
        CHECK(t.size() == 16 + 508 && t.compare(0, 16, "MS2000R-PATCH-1:") == 0);
        std::vector<uint8_t> back;
        CHECK(PresetBank::fromClipboardText(t, back) && back == p);
        CHECK(PresetBank::fromClipboardText("  \n" + t + "\r\n ", back) && back == p);            // whitespace around is fine
        std::string lo = t; for (auto& c : lo) c = char(std::tolower(static_cast<unsigned char>(c))); lo.replace(0, 16, "MS2000R-PATCH-1:");
        CHECK(PresetBank::fromClipboardText(lo, back) && back == p);                              // lower case hex too
        CHECK(!PresetBank::fromClipboardText(t.substr(0, t.size() - 2), back));                   // short
        CHECK(!PresetBank::fromClipboardText(t + "00", back));                                    // long
        CHECK(!PresetBank::fromClipboardText("MS2000R-PATCH-2:" + t.substr(16), back));           // other tag
        CHECK(!PresetBank::fromClipboardText(t.substr(0, 20) + "ZZ" + t.substr(22), back));       // not hex
        CHECK(!PresetBank::fromClipboardText("hello", back) && !PresetBank::fromClipboardText("", back));
    }

    // the bank's own file
    const auto ser = b.serialize();
    CHECK(ser.size() == 10 + 3 * 254);
    PresetBank r; CHECK(r.deserialize(ser) && r.size() == 3 && r.at(2).data == b.at(2).data && r.name(0) == "Three");
    auto bad = ser; bad[3] = 'X'; CHECK(!r.deserialize(bad));
    bad = ser; bad.pop_back(); CHECK(!r.deserialize(bad));
    bad = ser; bad[9] = 200; CHECK(!r.deserialize(bad));
    CHECK(r.size() == 3);                                                       // a refused file leaves the bank alone

    std::printf(g_fail ? "preset_bank: %d FAILED\n" : "preset_bank: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
