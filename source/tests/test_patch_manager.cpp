// test_patch_manager.cpp - PATCHES-1: the patch manager (core/patch_manager.h) on a real folder, no machine needed.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include "core/patch_manager.h"

using namespace MS2000;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static std::vector<uint8_t> prog(const std::string& name, uint8_t seed)
{
    std::vector<uint8_t> p(kBankProgramBytes);
    for (size_t i = 0; i < p.size(); ++i) p[i] = uint8_t((seed * 31u + i * 7u) & 0xFF);
    const std::string s = PresetBank::sanitizeName(name);
    for (size_t i = 0; i < 12; ++i) p[i] = uint8_t(s[i]);
    return p;
}
static PresetBank bankOf(int n, const std::string& tag)
{
    PresetBank b;
    for (int i = 0; i < n; ++i) b.add(prog(tag + std::to_string(i), uint8_t(i)));
    return b;
}
static bool fileThere(const fs::path& p) { std::error_code ec; return fs::exists(p, ec); }

int main()
{
    // a root whose name is Cyrillic ("Papka"), as a Windows user name may be
    const fs::path base = fs::temp_directory_path() / pathFromUtf8("ms2k_pm_test_\xD0\x9F\xD0\xB0\xD0\xBF\xD0\xBA\xD0\xB0");
    std::error_code ec;
    fs::remove_all(base, ec);
    const fs::path root = base / "patchmanager";
    const fs::path other = base / "elsewhere";
    fs::create_directories(other, ec);

    // names
    CHECK(PatchManager::sanitizeFileName("a/b:c*d") == "a_b_c_d");
    CHECK(PatchManager::sanitizeFileName("  .x. ") == "x");
    CHECK(PatchManager::sanitizeFileName("...").empty());
    CHECK(PatchManager::sanitizeFileName(std::string(100, 'a')).size() == 60);

    // first run: nothing there, the folder is made
    bool first = false;
    PatchManager pm(root);
    CHECK(pm.load(&first) && first && pm.userCount() == 0 && pm.sourceCount() == 0 && fileThere(root));

    // create, fill, save; a second manager sees the same
    const int a = pm.createBank("TMZ");
    CHECK(a == 0 && fileThere(root / "TMZ.ms2kbank") && pm.user(0).name == "TMZ");
    const int a2 = pm.createBank("TMZ");
    CHECK(a2 == 1 && pm.user(1).name == "TMZ 2" && fileThere(root / "TMZ 2.ms2kbank"));
    CHECK(pm.createBank("bad/name?") == 2 && pm.user(2).name == "bad_name_");
    pm.user(0).bank.add(prog("Pad One", 1)); pm.user(0).bank.add(prog("Pad Two", 2));
    CHECK(pm.saveBank(0) && fileThere(root / "library.txt"));
    {
        PatchManager q(root);
        bool f2 = true;
        CHECK(q.load(&f2) && !f2 && q.userCount() == 3 && q.user(0).name == "TMZ" && q.user(0).bank.size() == 2);
        CHECK(q.user(0).bank.name(1) == "Pad Two" && q.user(0).bank.at(0).data == prog("Pad One", 1));
        CHECK(q.userTotal() == 2);
    }

    // rename: the file is renamed; a taken name or an empty one is refused; a case-only change is allowed
    CHECK(pm.renameBank(0, "TMZ pads") && pm.user(0).name == "TMZ pads" && fileThere(root / "TMZ pads.ms2kbank") && !fileThere(root / "TMZ.ms2kbank"));
    CHECK(!pm.renameBank(0, "tmz 2") && !pm.renameBank(0, "///") == false);       // "///" -> "___": legal
    CHECK(pm.user(0).name == "___");
    CHECK(pm.renameBank(0, "TMZ pads") && pm.renameBank(0, "TMZ Pads") && pm.user(0).name == "TMZ Pads" && fileThere(root / "TMZ Pads.ms2kbank"));
    CHECK(!pm.renameBank(0, "") && !pm.renameBank(0, "   ") && !pm.renameBank(0, "TMZ 2") && !pm.renameBank(9, "x"));
    CHECK(pm.user(0).bank.size() == 2);
    {   PatchManager q(root); q.load(); CHECK(q.user(0).name == "TMZ Pads" && q.user(0).bank.size() == 2); }

    // refresh: the file changed on disk, the list follows
    {
        PresetBank nb = bankOf(5, "Ext");
        CHECK(PatchManager::writeAtomic(root / "TMZ 2.ms2kbank", nb.serialize()));
        CHECK(pm.user(1).bank.size() == 0 && pm.refreshBank(1) && pm.user(1).bank.size() == 5 && pm.user(1).bank.name(4) == "Ext4");
        CHECK(PatchManager::writeAtomic(root / "TMZ 2.ms2kbank", std::vector<uint8_t>{ 1, 2, 3 }));
        CHECK(!pm.refreshBank(1) && pm.user(1).bank.size() == 5);                   // an unreadable file leaves the bank alone
        CHECK(PatchManager::writeAtomic(root / "TMZ 2.ms2kbank", nb.serialize()));  // put the good file back
    }

    // remove (the file stays) / add it back / delete (the file goes)
    CHECK(pm.removeBank(2) && pm.userCount() == 2 && fileThere(root / "bad_name_.ms2kbank"));
    { PatchManager q(root); q.load(); CHECK(q.userCount() == 2); }                  // not listed any more
    CHECK(pm.addBankFile(root / "bad_name_.ms2kbank") == 2 && pm.userCount() == 3);
    CHECK(pm.addBankFile(root / "bad_name_.ms2kbank") == 2 && pm.userCount() == 3); // listed once
    CHECK(pm.addBankFile(root / "nothing.ms2kbank") == -1);
    CHECK(pm.deleteBank(2) && pm.userCount() == 2 && !fileThere(root / "bad_name_.ms2kbank"));
    CHECK(!pm.removeBank(7) && !pm.deleteBank(7));

    // data sources: a .syx (128), a .mid, a file with several dumps (384), a file that is no MS2000 data
    const PresetBank s1 = bankOf(128, "Syx"), s2 = bankOf(128, "Mid");
    const auto syx = s1.toSyx();
    auto big = syx; { const auto m2 = s2.toSyx(); big.insert(big.end(), m2.begin(), m2.end()); const auto m3 = s1.toSyx(); big.insert(big.end(), m3.begin(), m3.end()); }
    CHECK(PresetBank::writeFile(other / "one.syx", syx));
    CHECK(PresetBank::writeFile(other / "two.mid", s2.toMidi("two")));
    CHECK(PresetBank::writeFile(other / "big.syx", big));
    CHECK(PresetBank::writeFile(other / "junk.syx", std::vector<uint8_t>{ 0xF0, 0x43, 0x01, 0xF7 }));
    std::string err;
    CHECK(pm.addSource(other / "one.syx", err) == 0 && pm.source(0).count == 128 && pm.source(0).loaded);
    CHECK(pm.addSource(other / "one.syx", err) == 0 && pm.sourceCount() == 1);       // listed once
    CHECK(pm.addSource(other / "junk.syx", err) == -1 && !err.empty() && pm.sourceCount() == 1);
    CHECK(pm.addSource(other / "big.syx", err) == 1 && pm.source(1).count == 384);
    CHECK(pm.addSourcesFromFolder(other) == 1 && pm.sourceCount() == 3 && pm.sourceTotal() == 128 + 384 + 128);   // only two.mid is new
    CHECK(fileThere(pm.cacheFile(pm.source(0))));
    {   // a new manager: counts come from the registry, the programs from the cache
        PatchManager q(root); q.load();
        CHECK(q.sourceCount() == 3 && q.source(0).count == 128 && !q.source(0).loaded && !q.source(0).missing);
        CHECK(q.loadSource(0, err) && q.source(0).loaded && q.source(0).progs.size() == 128 && q.source(0).progs[5] == s1.at(5).data);
        CHECK(q.loadSource(1, err) && q.source(1).progs.size() == 384 && q.source(1).progs[130] == s2.at(2).data);
        CHECK(q.loadSource(2, err) && q.source(2).progs[127] == s2.at(127).data);
    }
    {   // the source file is gone: the cache still lists it, refresh says so
        fs::remove(other / "two.mid", ec);
        PatchManager q(root); q.load();
        CHECK(q.source(2).missing);
        CHECK(q.loadSource(2, err) && q.source(2).progs.size() == 128 && q.source(2).missing);
        CHECK(!q.refreshSource(2, err) && q.source(2).missing);
    }
    {   // the source file changed: the cache is stale, the file is read again
        const PresetBank more = bankOf(3, "New");
        auto grown = syx; const auto mm = more.toSyx(); grown.insert(grown.end(), mm.begin(), mm.end());
        CHECK(PresetBank::writeFile(other / "one.syx", grown));
        PatchManager q(root); q.load();
        CHECK(q.loadSource(0, err) && q.source(0).progs.size() == 256 && q.source(0).count == 256);
        PatchManager q2(root); q2.load();
        CHECK(q2.source(0).count == 256);                                            // the registry follows
        CHECK(PresetBank::writeFile(other / "one.syx", syx));
        CHECK(q2.refreshSource(0, err) && q2.source(0).progs.size() == 128);
    }
    CHECK(pm.removeSource(1) && pm.sourceCount() == 2 && !fileThere(pm.cacheFile(pm.source(0)) .parent_path() / "x"));
    CHECK(!pm.removeSource(5));

    // import: a .syx of 384 programs becomes three banks of 128; a .ms2kbank one bank
    {
        const size_t before = pm.userCount();
        const auto made = pm.importFile(other / "big.syx", err);
        CHECK(made.size() == 3 && pm.userCount() == before + 3);
        CHECK(pm.user(size_t(made[0])).name == "big" && pm.user(size_t(made[1])).name == "big 2" && pm.user(size_t(made[2])).name == "big 3");
        CHECK(pm.user(size_t(made[1])).bank.size() == 128 && pm.user(size_t(made[1])).bank.at(0).data == s2.at(0).data);
        const auto one = pm.importFile(root / "TMZ Pads.ms2kbank", err);
        CHECK(one.size() == 1 && pm.user(size_t(one[0])).name == "TMZ Pads 2" && pm.user(size_t(one[0])).bank.size() == 2);
        CHECK(pm.importFile(other / "junk.syx", err).empty() && !err.empty());
        CHECK(pm.importFile(other / "missing.syx", err).empty());
    }

    // the first plug-in version's single bank moves into the new layout
    {
        const fs::path root2 = base / "second";
        PatchManager q(root2);
        bool f3 = false;
        q.load(&f3);
        CHECK(f3);
        CHECK(PresetBank::writeFile(other / "userbank.ms2kbank", bankOf(4, "Old").serialize()));
        CHECK(q.importLegacy(other / "userbank.ms2kbank", "User Bank") == 0 && q.user(0).bank.size() == 4 && q.user(0).name == "User Bank");
        CHECK(q.importLegacy(other / "none.ms2kbank", "x") == -1);
    }

    // no registry yet: the banks lying in the folder are listed, in name order
    {
        const fs::path root3 = base / "third";
        fs::create_directories(root3, ec);
        CHECK(PatchManager::writeAtomic(root3 / "b.ms2kbank", bankOf(1, "B").serialize()));
        CHECK(PatchManager::writeAtomic(root3 / "a.ms2kbank", bankOf(2, "A").serialize()));
        CHECK(PatchManager::writeAtomic(root3 / "note.txt", std::vector<uint8_t>{ 'x' }));
        PatchManager q(root3);
        CHECK(q.load() && q.userCount() == 2 && q.user(0).name == "a" && q.user(1).name == "b");
    }

    // Cyrillic bank name in a Cyrillic folder
    {
        const std::string nm = "\xD0\x91\xD0\xB0\xD0\xBD\xD0\xBA \xD0\xBF\xD1\x8D\xD0\xB4\xD0\xBE\xD0\xB2";   // "Bank pedov"
        const int i = pm.createBank(nm);
        CHECK(i >= 0 && pm.user(size_t(i)).name == nm && fileThere(root / pathFromUtf8(nm + ".ms2kbank")));
        PatchManager q(root); q.load();
        CHECK(q.indexOfUserFile(root / pathFromUtf8(nm + ".ms2kbank")) >= 0);
    }

    fs::remove_all(base, ec);
    std::printf(g_fail ? "patch_manager: %d FAILED\n" : "patch_manager: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
