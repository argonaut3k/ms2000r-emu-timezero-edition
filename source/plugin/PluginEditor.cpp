// MS2000R VST3 editor (phase 2, VST3-2). See PluginEditor.h.
//
// The panel is immediate-mode (vector_panel.h): it is walked twice. The INPUT pass runs on every mouse event
// with a backend that draws nothing and answers item() from the mouse - switches and knobs change there, at
// once, so a click shorter than a frame still reaches the machine. The DRAW pass runs in paint() with a
// backend on juce::Graphics whose items are passive (no new presses, no drag), so drawing changes nothing.
// The timer (30 Hz) reads the LEDs and the LCD and repaints only when something the panel shows changed.
#include "PluginEditor.h"
#include <cstring>

namespace {
constexpr int kTabH = 30;
const juce::Colour kBg(28, 34, 38);

juce::Colour col(uint32_t c) { return juce::Colour(juce::uint8(c), juce::uint8(c >> 8), juce::uint8(c >> 16), juce::uint8(c >> 24)); }

// the input pass: no drawing, items from the mouse
struct InputBackend final : VPanel::Backend {
    Ms2kEditor::Mouse& m;
    explicit InputBackend(Ms2kEditor::Mouse& mm) : m(mm) {}
    void rectFilled(VPanel::V2, VPanel::V2, uint32_t, float) override {}
    void rect(VPanel::V2, VPanel::V2, uint32_t, float, float) override {}
    void circleFilled(VPanel::V2, float, uint32_t, int) override {}
    void circle(VPanel::V2, float, uint32_t, int, float) override {}
    void line(VPanel::V2, VPanel::V2, uint32_t, float) override {}
    VPanel::V2 textSize(float, const char*) override { return {}; }
    void text(VPanel::V2, float, uint32_t, const char*) override {}
    VPanel::Item item(const char* id, VPanel::V2 a, VPanel::V2 b) override
    {
        const bool hit = m.inside && m.pos.x >= a.x && m.pos.x < b.x && m.pos.y >= a.y && m.pos.y < b.y;
        if (m.press && hit && m.active.empty()) m.active = id;
        VPanel::Item it;
        it.active = !m.active.empty() && m.active == id;
        it.hovered = hit && (m.active.empty() || it.active);
        it.shift = m.shift;
        it.dblClick = it.hovered && m.dbl;
        it.dragY = it.active ? m.dragY : 0.0f;
        it.wheel = it.hovered ? m.wheel : 0.0f;
        return it;
    }
    void tooltip(const char*) override {}
    void lcd(VPanel::V2, VPanel::V2) override {}
};

// the draw pass: juce::Graphics, passive items
struct DrawBackend final : VPanel::Backend {
    juce::Graphics& g; const Ms2kEditor::Mouse& m; juce::Typeface::Ptr face;
    std::function<void(juce::Rectangle<float>)> lcdFn;
    juce::String tip;
    float fontPx = -1.0f; juce::Font font;
    // consecutive lines of one colour and width are stroked as ONE path (a pot's 36 knurl lines, its 11 scale
    // ticks): one edge table instead of one per line
    juce::Path lines; uint32_t linesCol = 0; float linesW = -1.0f;
    // which layer this pass draws: 0 = the printed panel (cached as a picture), 1 = what changes, 2 = both
    int pass = 2; bool dyn = false;
    bool skip() const { return pass != 2 && (pass == 1) != dyn; }
    void layer(bool d) override { if (d != dyn) { flush(); dyn = d; } }
    DrawBackend(juce::Graphics& gg, const Ms2kEditor::Mouse& mm, juce::Typeface::Ptr f) : g(gg), m(mm), face(f), font(14.0f) {}
    void flush()
    {
        if (lines.isEmpty()) return;
        g.setColour(col(linesCol));
        g.strokePath(lines, juce::PathStrokeType(linesW));
        lines.clear();
    }
    static juce::Rectangle<float> R(VPanel::V2 a, VPanel::V2 b) { return { a.x, a.y, b.x - a.x, b.y - a.y }; }
    const juce::Font& fontAt(float px)
    {
        if (px != fontPx) { fontPx = px; font = face ? juce::Font(face).withHeight(px) : juce::Font(px, juce::Font::bold); }
        return font;
    }
    void rectFilled(VPanel::V2 a, VPanel::V2 b, uint32_t c, float r) override
    {
        if (skip()) return;
        flush();
        g.setColour(col(c));
        if (r > 0.0f) g.fillRoundedRectangle(R(a, b), r); else g.fillRect(R(a, b));
    }
    void rect(VPanel::V2 a, VPanel::V2 b, uint32_t c, float r, float t) override
    {
        if (skip()) return;
        flush();
        g.setColour(col(c));
        const auto q = R(a, b).reduced(t * 0.5f);
        if (r > 0.0f) g.drawRoundedRectangle(q, r, t); else g.drawRect(q, t);
    }
    void circleFilled(VPanel::V2 c, float r, uint32_t cc, int) override { if (skip()) return; flush(); g.setColour(col(cc)); g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r); }
    void circle(VPanel::V2 c, float r, uint32_t cc, int, float t) override { if (skip()) return; flush(); g.setColour(col(cc)); g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r, t); }
    void line(VPanel::V2 a, VPanel::V2 b, uint32_t c, float t) override
    {
        if (skip()) return;
        if (c != linesCol || t != linesW) { flush(); linesCol = c; linesW = t; }
        lines.startNewSubPath(a.x, a.y); lines.lineTo(b.x, b.y);
    }
    VPanel::V2 textSize(float px, const char* t) override { const auto& f = fontAt(px); return { f.getStringWidthFloat(t), px }; }
    void text(VPanel::V2 p, float px, uint32_t c, const char* t) override
    {
        if (skip()) return;
        flush();
        const auto& f = fontAt(px);
        g.setFont(f); g.setColour(col(c));
        g.drawSingleLineText(t, int(std::lround(p.x)), int(std::lround(p.y + f.getAscent())));
    }
    VPanel::Item item(const char* id, VPanel::V2 a, VPanel::V2 b) override
    {
        const bool hit = m.inside && m.pos.x >= a.x && m.pos.x < b.x && m.pos.y >= a.y && m.pos.y < b.y;
        VPanel::Item it;
        it.active = !m.active.empty() && m.active == id;
        it.hovered = hit && (m.active.empty() || it.active);
        return it;
    }
    void tooltip(const char* t) override { tip = t; }
    void lcd(VPanel::V2 a, VPanel::V2 b) override { if (pass == 0) return; flush(); if (lcdFn) lcdFn(R(a, b)); }
};

juce::Typeface::Ptr loadPanelFace()
{
    // the standalone's panel font: a narrow bold sans from the OS (the same files, the same order - gui_platform_*)
#ifdef _WIN32
    for (const char* p : { "C:\\Windows\\Fonts\\ARIALNB.TTF", "C:\\Windows\\Fonts\\arialbd.ttf", "C:\\Windows\\Fonts\\segoeuib.ttf" }) {
#else
    for (const char* p : { "/System/Library/Fonts/Supplemental/Arial Narrow Bold.ttf", "/Library/Fonts/Arial Narrow Bold.ttf",
                           "/System/Library/Fonts/Supplemental/Arial Bold.ttf" }) {
#endif
        juce::MemoryBlock mb;
        if (juce::File(p).loadFileAsData(mb) && mb.getSize() > 0)
            if (auto tf = juce::Typeface::createSystemTypefaceFor(mb.getData(), mb.getSize())) return tf;
    }
    return {};
}
} // namespace

Ms2kEditor::Ms2kEditor(Ms2kProcessor& p) : AudioProcessorEditor(p), m_proc(p)
{
    m_face = loadPanelFace();
    // HD44780 A00 character generator, if the local dump is present (thin_gui's loadCgRom: 16 bytes a
    // character, rows 0-7, 5 low bits). Without it the LCD is drawn as text. The working directory is the
    // MS2000 folder (the processor set it).
    for (const char* f : { "hd44780_a00.bin", "full FW/hd44780_a00.bin" }) {   // ROM-PATHS-1
        juce::MemoryBlock mb;
        if (juce::File::getCurrentWorkingDirectory().getChildFile(f).loadFileAsData(mb) && mb.getSize() >= 4096) {
            const auto* b = static_cast<const uint8_t*>(mb.getData());
            for (int c = 0; c < 256; ++c) for (int y = 0; y < 8; ++y) m_cg[c][y] = b[c * 16 + y] & 0x1F;
            m_cgOk = true;
        }
    }
    setupIo();

    for (auto* b : { &m_tabPanel, &m_tabSettings, &m_tabPatches }) { b->setClickingTogglesState(false); b->setRadioGroupId(0); addAndMakeVisible(*b); }
    m_tabPatches.onClick = [this] { showTab(2); };
    m_tabPanel.onClick = [this] { showTab(0); };
    m_tabSettings.onClick = [this] { showTab(1); };
    m_mic2.setToggleState(m_proc.mic2, juce::dontSendNotification);
    m_dac20.setToggleState(m_proc.dac20, juce::dontSendNotification);
    m_clock.setToggleState(m_proc.hostClock.load(), juce::dontSendNotification);
    m_clock.onClick = [this] { m_proc.hostClock = m_clock.getToggleState(); m_proc.saveGlobalSettings(); };
    m_dspThr.setToggleState(m_proc.dspThread.load(), juce::dontSendNotification);
    m_dspThr.onClick = [this] { m_proc.dspThread = m_dspThr.getToggleState(); m_proc.saveGlobalSettings(); };
    m_dspThr.setColour(juce::ToggleButton::textColourId, juce::Colour(236, 242, 244));
    addChildComponent(m_dspThr);
    m_latency.setToggleState(m_proc.latencyReport.load(), juce::dontSendNotification);
    m_latency.onClick = [this] { m_proc.latencyReport = m_latency.getToggleState(); m_proc.applyLatency(); m_proc.saveGlobalSettings(); };
    m_latency.setColour(juce::ToggleButton::textColourId, juce::Colour(236, 242, 244));
    addChildComponent(m_latency);
    m_follow.setToggleState(m_proc.knobFollow.load(), juce::dontSendNotification);
    m_follow.onClick = [this] { m_proc.knobFollow = m_follow.getToggleState(); m_proc.saveGlobalSettings(); refreshKnobs(); m_dirty = true; };
    m_follow.setColour(juce::ToggleButton::textColourId, juce::Colour(236, 242, 244));
    addChildComponent(m_follow);
    m_transport.setToggleState(m_proc.transportMsgs.load(), juce::dontSendNotification);
    m_transport.onClick = [this] { m_proc.transportMsgs = m_transport.getToggleState(); m_proc.saveGlobalSettings(); };
    m_transport.setColour(juce::ToggleButton::textColourId, juce::Colour(236, 242, 244));
    addChildComponent(m_transport);
    m_sysexOut.setToggleState(m_proc.sysexOut.load(), juce::dontSendNotification);
    m_sysexOut.onClick = [this] { m_proc.sysexOut = m_sysexOut.getToggleState(); m_proc.saveGlobalSettings(); };
    m_sysexOut.setColour(juce::ToggleButton::textColourId, juce::Colour(236, 242, 244));
    addChildComponent(m_sysexOut);
    m_powerSw.setToggleState(m_proc.powerSwitch.load(), juce::dontSendNotification);
    m_powerSw.onClick = [this] { m_proc.powerSwitch = m_powerSw.getToggleState(); m_proc.saveGlobalSettings(); };
    m_powerSw.setColour(juce::ToggleButton::textColourId, juce::Colour(236, 242, 244));
    addChildComponent(m_powerSw);
    // OUT-BOOST-1: master output boost after the VOLUME pot, 0..+12 dB in 1 dB steps (global default + project)
    m_boost.setSliderStyle(juce::Slider::LinearHorizontal);
    m_boost.setRange(0.0, 12.0, 1.0);
    m_boost.setTextValueSuffix(" dB");
    m_boost.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 22);
    m_boost.setValue(double(m_proc.boostDb.load()), juce::dontSendNotification);
    m_boost.onValueChange = [this] { m_proc.boostDb = int(m_boost.getValue()); m_proc.applyVolume(); m_proc.saveGlobalSettings(); };
    addChildComponent(m_boost);
    m_boostLbl.setText("Output boost after the VOLUME pot (+dB, above 0 dB full scale the host clips)", juce::dontSendNotification);
    m_boostLbl.setColour(juce::Label::textColourId, juce::Colour(236, 242, 244));
    addChildComponent(m_boostLbl);
    // PATCHES-1
    for (auto* x : std::initializer_list<juce::Component*>{ &m_treeView, &m_gridView, &m_pmStatus }) addChildComponent(*x);
    m_treeView.setViewedComponent(&m_tree, false);
    m_treeView.setScrollBarsShown(true, false);                      // a scrollbar when the banks do not fit
    m_gridView.setViewedComponent(&m_grid, false);
    m_gridView.setScrollBarsShown(false, true);                      // the columns run to the right
    m_pmStatus.setColour(juce::Label::textColourId, juce::Colour(236, 242, 244));
    m_pmStatus.setMinimumHorizontalScale(1.0f);
    m_toast.setColour(juce::Label::textColourId, juce::Colour(240, 170, 60));
    m_toast.setFont(juce::Font(15.0f, juce::Font::bold));
    m_toast.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(m_toast);
    m_tree.onSelect = [this](Ms2kPatchTree::Kind k, int i) { pmSelect(k, i); };
    m_tree.onContext = [this](Ms2kPatchTree::Kind k, int i) { pmTreeMenu(k, i); };
    m_tree.onAddBank = [this] { pmNewBank(); };
    m_tree.onAddSources = [this] {
        juce::PopupMenu m;
        m.addItem("Add Files...", [this] { pmAddSources(); });
        m.addItem("Add Folder...", [this] { pmAddFolder(); });
        m.showMenuAsync(juce::PopupMenu::Options());
    };
    m_tree.onRenamed = [this](int i, const juce::String& t) { pmRenameBank(i, t); };
    m_grid.onLoad = [this](int row) { pmLoadProgram(row); };
    m_grid.onRenamed = [this](int row, const juce::String& t) { pmRenameProgram(row, t); };
    m_grid.onMove = [this](int row, int gap) { pmMoveProgram(row, gap); };
    m_grid.onContext = [this](int row) { pmGridMenu(row); };
    m_grid.onStep = [this](int delta) { pmStep(delta); };
    m_grid.emptyText = "Select a bank on the left.";
    m_pmLastSyx = juce::String(m_proc.syx().status());
    pmStatus("Click a program (or step with the Up / Down arrows) to send it to the synth. Right-click for the menus (Rename, Delete...), drag a name to reorder.");
    m_mic2.onClick = [this] { m_proc.mic2 = m_mic2.getToggleState(); m_proc.userInputs(); m_proc.endUserEdits(); m_proc.saveGlobalSettings(); };
    m_dac20.onClick = [this] { m_proc.dac20 = m_dac20.getToggleState(); m_proc.applyDac(); m_proc.saveGlobalSettings(); };
    m_syxLoad.onClick = [this] {
        m_chooser = std::make_unique<juce::FileChooser>("Load a .syx into the MS2000R", juce::File(), "*.syx");
        m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f.existsAsFile()) m_proc.syx().startImport(f.getFullPathName().toStdString());
        });
    };
    m_syxSave.onClick = [this] {
        m_chooser = std::make_unique<juce::FileChooser>("Save all 128 programs", juce::File(), "*.syx");
        m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            if (!f.hasFileExtension("syx")) f = f.withFileExtension("syx");
            m_proc.syx().startExport(f.getFullPathName().toStdString());
        });
    };
    m_homeBtn.onClick = [this] {
        m_chooser = std::make_unique<juce::FileChooser>("The folder with flash.bin and boot-362.bin (beside it or in 'full FW')", juce::File(m_proc.homePath()));
        m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
            const auto d = fc.getResult();
            if (d.isDirectory()) { m_proc.chooseHome(d); m_dirty = true; }
        });
    };
    m_demo.onClick = [this] { if (m_demoPhase == 0) { m_demoPhase = 1; m_demoT0 = juce::Time::getMillisecondCounter(); } };
    m_help.setText("Panel: drag a knob up / down (Shift = fine), mouse wheel, double-click = centre.\n"
                   "Shift+click a program key 1-16 or EXIT: it stays held (amber ring) - chords on the pads, or EXIT held while "
                   "you press GLOBAL (demo songs). Click it again to let it go.\n"
                   "POWER / VOLUME is the plugin's output level; AUDIO IN 1 / 2 are the input level pots. The host routes "
                   "its input bus to AUDIO IN 1 (left) and 2 (right).", juce::dontSendNotification);
    m_help.setJustificationType(juce::Justification::topLeft);
    for (auto* c : std::initializer_list<juce::Component*>{ &m_mic2, &m_dac20, &m_clock, &m_demo, &m_syxLoad, &m_syxSave, &m_syxStatus, &m_homeBtn, &m_help, &m_status }) addChildComponent(*c);
    m_clock.setColour(juce::ToggleButton::textColourId, juce::Colour(236, 242, 244));
    m_syxStatus.setColour(juce::Label::textColourId, juce::Colour(236, 242, 244));
    for (auto* c : std::initializer_list<juce::Component*>{ &m_mic2, &m_dac20, &m_help, &m_status })
        c->setColour(juce::Label::textColourId, juce::Colour(236, 242, 244)), c->setColour(juce::ToggleButton::textColourId, juce::Colour(236, 242, 244));

    // UI-SIZE-1: the project's own width, else the user's last width (settings file), else the large default.
    // Read BEFORE setResizeLimits: that call clamps the empty editor to 700 x 380, resized() stores the 700 in
    // editorW, and every reopen came up at the minimum (VSTHost: lost on closing the GUI; Cubase: on reload).
    const int w = m_proc.editorW >= 700 ? m_proc.editorW : m_proc.editorWDefault >= 700 ? m_proc.editorWDefault : 1400;
    setResizable(true, true);
    setResizeLimits(700, 380, 3840, 2160);
    setSize(w, int(std::lround(w * VPanel::kH / VPanel::kW)) + kTabH);
    // The host checks a size request through this constrainer (JUCE's VST3 wrapper: fixed aspect ratio only).
    // resized() keeps the ratio equal to the current size's, so it follows the panel at every width.
    m_constrainer.setMinimumSize(700, 380);
    m_constrainer.setMaximumSize(3840, 2160);
    setConstrainer(&m_constrainer);
    showTab(m_proc.editorTab >= 0 && m_proc.editorTab <= 2 ? m_proc.editorTab : 0);
    if (const char* e = std::getenv("MS2K_EDITORSHOT"); e && *e) { m_shotPath = e; m_testTick = 0; showTab(0); }
    if (const char* e = std::getenv("MS2K_EDITORTEST"); e && *e) m_testMode = e;
    if (m_testMode == "follow") m_proc.knobFollow = true;
    startTimerHz(30);
}

Ms2kEditor::~Ms2kEditor()
{
    setConstrainer(nullptr);   // the member constrainer goes before the base class does
    juce::PopupMenu::dismissAllActiveMenus();   // PATCHES-1: an open menu calls back into this editor
    stopTimer();
    if (getWidth() >= 700) { m_proc.editorWDefault = getWidth(); m_proc.saveGlobalSettings(); }   // UI-SIZE-1
    if (m_demoPhase) { m_proc.setSwitch(3, 6, false); m_proc.setSwitch(4, 0, false); m_demoPhase = 0; }
    m_io.releaseAll();   // STATED: a key held by the mouse or LATCH is let go when the editor closes
    m_proc.endUserEdits();   // AUTOMATION-1
}

void Ms2kEditor::setupIo()
{
    m_io.lit = m_lit; m_io.shown = m_shown; m_io.knobs = m_proc.knobs;
    m_io.volume = &m_proc.volume; m_io.in1 = &m_proc.in1; m_io.in2 = &m_proc.in2;
    m_io.sw = [this](unsigned c, unsigned r, bool d) { if (m_demoPhase == 0) m_proc.userKey(c, r, d); };   // AUTOMATION-1
    m_io.knob = [this](unsigned m, unsigned x, uint16_t v) { m_proc.userKnob(m, x, v); m_lastMoved = int(m * 8 + x); m_lastMoveMs = juce::Time::getMillisecondCounter(); };
}

void Ms2kEditor::showTab(int t)
{
    m_tab = t; m_proc.editorTab = t;
    m_tabPanel.setToggleState(t == 0, juce::dontSendNotification);
    m_tabSettings.setToggleState(t == 1, juce::dontSendNotification);
    m_tabPatches.setToggleState(t == 2, juce::dontSendNotification);
    m_transport.setVisible(t == 1); m_sysexOut.setVisible(t == 1); m_powerSw.setVisible(t == 1); m_boost.setVisible(t == 1); m_boostLbl.setVisible(t == 1); m_follow.setVisible(t == 1); m_dspThr.setVisible(t == 1); m_latency.setVisible(t == 1);
    for (auto* c : std::initializer_list<juce::Component*>{ &m_treeView, &m_gridView, &m_pmStatus }) c->setVisible(t == 2);
    m_pmPendingRow = -1;
    if (t == 2) pmSyncDisk();
    for (auto* c : std::initializer_list<juce::Component*>{ &m_mic2, &m_dac20, &m_clock, &m_demo, &m_syxLoad, &m_syxSave, &m_syxStatus, &m_homeBtn, &m_help, &m_status }) c->setVisible(t == 1);
    if (t != 0) { m_io.releaseAll(); m_mouse.active.clear(); }
    m_dirty = true; repaint();
}

juce::Rectangle<float> Ms2kEditor::panelArea() const
{
    const auto a = getLocalBounds().withTrimmedTop(kTabH).toFloat();
    const float s = juce::jmin(a.getWidth() / VPanel::kW, a.getHeight() / VPanel::kH);
    return { a.getX() + (a.getWidth() - VPanel::kW * s) * 0.5f, a.getY(), VPanel::kW * s, VPanel::kH * s };
}

void Ms2kEditor::resized()
{
    m_tabPanel.setBounds(6, 4, 90, kTabH - 8);
    m_tabSettings.setBounds(100, 4, 90, kTabH - 8);
    m_tabPatches.setBounds(194, 4, 90, kTabH - 8);
    m_toast.setBounds(294, 4, juce::jmax(0, getWidth() - 304), kTabH - 8);
    {   // PATCHES-1
        auto pr = getLocalBounds().withTrimmedTop(kTabH);
        m_pmStatus.setBounds(pr.removeFromBottom(26).reduced(10, 2));
        m_treeView.setBounds(pr.removeFromLeft(juce::jmin(300, pr.getWidth() / 3)));
        m_tree.updateSize(m_treeView.getWidth() - m_treeView.getScrollBarThickness(), m_treeView.getHeight());   // the scrollbar always has its strip
        m_gridView.setBounds(pr);
        // the height always leaves room for the sideways scrollbar, so showing or hiding it never changes the rows per column
        m_grid.updateSize(m_gridView.getMaximumVisibleWidth(), m_gridView.getHeight() - m_gridView.getScrollBarThickness());
    }
    auto r = getLocalBounds().withTrimmedTop(kTabH).reduced(24, 16);
    m_mic2.setBounds(r.removeFromTop(30));
    m_dac20.setBounds(r.removeFromTop(30));
    m_clock.setBounds(r.removeFromTop(30));
    m_transport.setBounds(r.removeFromTop(30));
    m_sysexOut.setBounds(r.removeFromTop(30));
    m_powerSw.setBounds(r.removeFromTop(30));
    { auto row = r.removeFromTop(30); m_boost.setBounds(row.removeFromLeft(260)); m_boostLbl.setBounds(row); }
    m_latency.setBounds(r.removeFromTop(30));
    m_follow.setBounds(r.removeFromTop(30));
    m_dspThr.setBounds(r.removeFromTop(30));
    r.removeFromTop(10);
    m_demo.setBounds(r.removeFromTop(30).withWidth(300));
    r.removeFromTop(10);
    { auto row = r.removeFromTop(30); m_syxLoad.setBounds(row.removeFromLeft(180)); row.removeFromLeft(10); m_syxSave.setBounds(row.removeFromLeft(260)); }
    m_syxStatus.setBounds(r.removeFromTop(26));
    r.removeFromTop(6);
    m_homeBtn.setBounds(r.removeFromTop(30).withWidth(300));
    r.removeFromTop(16);
    m_help.setBounds(r.removeFromTop(110));
    m_status.setBounds(r.removeFromTop(48));
    // The panel's shape at this width: the window's height follows (the tab row adds kTabH). The ratio the host
    // checks is set from the same numbers, so a size request is corrected to it.
    if (m_tab == 0) {
        const int wantH = int(std::lround(getWidth() * VPanel::kH / VPanel::kW)) + kTabH;
        m_constrainer.setFixedAspectRatio(double(getWidth()) / double(wantH));
        if (std::abs(getHeight() - wantH) > 1) { setSize(getWidth(), wantH); return; }
    }
    m_proc.editorW = getWidth();
    m_dirty = true;
}

// ---- mouse -> the input pass ----
void Ms2kEditor::runInput()
{
    if (m_tab != 0) return;
    const auto pa = panelArea();
    InputBackend be(m_mouse);
    m_io.stageChanged = m_io.volumeChanged = false;
    VPanel::draw(m_io, be, VPanel::V2(pa.getX(), pa.getY()), VPanel::V2(pa.getWidth(), pa.getHeight()));
    if (m_io.stageChanged) m_proc.userInputs();
    if (m_io.volumeChanged) m_proc.userVolume();
    m_mouse.press = m_mouse.dbl = false; m_mouse.dragY = m_mouse.wheel = 0.0f;
    m_dirty = true; repaint();
}
static VPanel::V2 P(const juce::MouseEvent& e) { return { e.position.x, e.position.y }; }
void Ms2kEditor::mouseDown(const juce::MouseEvent& e)
{
    m_panelPopup = e.mods.isPopupMenu();
    if (m_panelPopup) { if (m_tab == 0) pmPanelMenu(); return; }          // PATCHES-1: the right button is the menu, not a knob
    m_mouse.pos = P(e); m_mouse.inside = true; m_mouse.btn = true; m_mouse.press = true;
    m_mouse.shift = e.mods.isShiftDown(); m_mouse.lastY = e.position.y; m_mouse.active.clear();
    runInput();
}
void Ms2kEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (m_panelPopup) return;
    m_mouse.pos = P(e); m_mouse.shift = e.mods.isShiftDown();
    m_mouse.dragY += e.position.y - m_mouse.lastY; m_mouse.lastY = e.position.y;
    runInput();
}
void Ms2kEditor::mouseUp(const juce::MouseEvent& e)
{
    if (m_panelPopup) { m_panelPopup = false; return; }
    m_mouse.pos = P(e); m_mouse.btn = false; m_mouse.active.clear();
    runInput();
}
void Ms2kEditor::mouseMove(const juce::MouseEvent& e) { m_mouse.pos = P(e); m_mouse.inside = true; m_mouse.shift = e.mods.isShiftDown(); m_dirty = true; repaint(); }
void Ms2kEditor::mouseExit(const juce::MouseEvent&) { m_mouse.inside = false; m_dirty = true; repaint(); }
void Ms2kEditor::mouseDoubleClick(const juce::MouseEvent& e) { if (e.mods.isPopupMenu()) return; m_mouse.pos = P(e); m_mouse.dbl = true; runInput(); }
void Ms2kEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // one notch = 1.0 as ImGui counts it (JUCE on Windows: 120 wheel units -> deltaY 0.234375)
    m_mouse.pos = P(e); m_mouse.inside = true; m_mouse.shift = e.mods.isShiftDown();
    m_mouse.wheel += (w.isReversed ? -w.deltaY : w.deltaY) / 0.234375f;
    runInput();
}

void Ms2kEditor::testClick(float px, float py, bool shift)
{
    const auto pa = panelArea();
    const float s = pa.getWidth() / VPanel::kW;
    m_mouse.pos = VPanel::V2(pa.getX() + px * s, pa.getY() + py * s); m_mouse.inside = true;
    m_mouse.btn = true; m_mouse.press = true; m_mouse.shift = shift; m_mouse.active.clear();
    runInput();
    m_mouse.btn = false; m_mouse.active.clear();
    runInput();
    m_mouse.inside = false;
}

// ---- the timer: LEDs, LCD, demo keys ----
void Ms2kEditor::timerCallback()
{
    // AUTOMATION-1: a host gesture lasts while the mouse button is down (wheel steps end on the next tick)
    if (!juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) m_proc.endUserEdits();
    if (!isShowing() && m_testTick < 0 && m_demoPhase == 0) return;   // DSP-THREAD b: nothing to draw while hidden/minimised
    if (m_testTick >= 0) {
        ++m_testTick;
        if (m_testTick == 60) {   // LCD-DOTS-1: MS2K_EDITORSHOT_W=<width> takes the shot at another editor width
            const char* sw = std::getenv("MS2K_EDITORSHOT_W");
            const int w = juce::jlimit(700, 3840, sw && *sw ? std::atoi(sw) : 1400);
            setSize(w, int(std::lround(float(w) * VPanel::kH / VPanel::kW)) + kTabH);
        }
        if (m_testMode == "follow" && m_proc.runner()) {   // the firmware's pot -> value, read back from the edit buffer
            auto cut = [&] { return unsigned(m_proc.runner()->getEmulator().peekExternal(MS2000::kEditBufferAddr + 58)); };
            static unsigned c0 = 0, c1 = 0, c2 = 0;
            if (m_testTick == 95) c0 = cut();
            if (m_testTick == 100) m_proc.setKnob(3, 7, 138);
            if (m_testTick == 130) c1 = cut();
            if (m_testTick == 140) m_proc.setKnob(3, 7, 1000);
            if (m_testTick == 170) { c2 = cut(); m_testResult = juce::String::formatted("cutoff byte: %u, pot 138 -> %u, pot 1000 -> %u", c0, c1, c2); }
        }
        if (m_testMode == "latch") {
            if (m_testTick == 120) testClick(258.0f, 855.0f, true);                   // pad 1
            if (m_testTick == 130) testClick(258.0f + 101.33f * 4, 855.0f, true);     // pad 5
            if (m_testTick == 140) testClick(1745.0f, 395.0f, true);                  // EXIT
        }
        if (m_testTick == 180) {
            const float sc = juce::jmax(1.0f, juce::Component::getApproximateScaleFactorForComponent(this));
            const juce::Image img = createComponentSnapshot(getLocalBounds(), true, sc);
            juce::File(m_shotPath + ".txt").replaceWithText(m_proc.status() + juce::String::formatted(
                "\neditor %d x %d (scale %.2f), last panel draw %.1f ms\ndrawn pots: CUTOFF %u RESONANCE %u EG1 ATTACK %u LEVEL %u TEMPO %u (follow %d)\n",
                getWidth(), getHeight(), double(sc), m_paintMs, unsigned(m_io.knobs[3][7]), unsigned(m_io.knobs[3][2]), unsigned(m_io.knobs[0][4]),
                unsigned(m_io.knobs[3][3]), unsigned(m_io.knobs[3][0]), int(m_proc.knobFollow.load())) + m_testResult + "\n");
            juce::File f(m_shotPath); f.deleteFile();
            juce::FileOutputStream os(f);
            juce::PNGImageFormat png;
            if (os.openedOk()) png.writeImageToStream(img, os);
            m_testTick = -1;
        }
    }
    if (m_demoPhase) {   // the standalone's demo button: EXIT down, GLOBAL down 200 ms later, both up after 1.4 s
        const auto dt = juce::Time::getMillisecondCounter() - m_demoT0;
        if (m_demoPhase == 1) { m_proc.setSwitch(4, 0, true); m_demoPhase = 2; }
        else if (m_demoPhase == 2 && dt >= 200) { m_proc.setSwitch(3, 6, true); m_demoPhase = 3; }
        else if (m_demoPhase == 3 && dt >= 1400) { m_proc.setSwitch(3, 6, false); m_proc.setSwitch(4, 0, false); m_demoPhase = 0; }
    }
    bool changed = m_dirty;
    if (auto* r = m_proc.runner()) {
        const auto leds = r->getEmulator().panelLeds();
        for (int i = 0; i < 8; ++i) for (int j = 0; j < 12; ++j) {
            if (std::abs(leds.lit[i][j] - m_lit[i][j]) > 1.0f / 64.0f) changed = true;
            m_lit[i][j] = leds.lit[i][j];
        }
        const LcdGuiSnapshot s = r->getLcdGuiSnapshot();
        if (std::memcmp(s.line0, m_lcd.line0, sizeof s.line0) || std::memcmp(s.line1, m_lcd.line1, sizeof s.line1) ||
            std::memcmp(s.cgram, m_lcd.cgram, sizeof s.cgram) || s.displayOn != m_lcd.displayOn) changed = true;
        m_lcd = s;
    } else if (m_lcd.displayOn || std::any_of(&m_lit[0][0], &m_lit[0][0] + 8 * 12, [](float v) { return v != 0.0f; })) {
        // PWR-SW-1: no machine (switched off) - the LCD glass and every LED go dark, nothing keeps its last state
        for (int i = 0; i < 8; ++i) for (int j = 0; j < 12; ++j) m_lit[i][j] = 0.0f;
        m_lcd.displayOn = false;
        changed = true;
    }
    if (m_toastUntil != 0 && juce::int32(juce::Time::getMillisecondCounter() - m_toastUntil) > 0) { m_toast.setText("", juce::dontSendNotification); m_toastUntil = 0; }
    if (m_tab == 2) {
        // what the machine answered to a program just sent (DATA LOAD COMPLETED / ERROR ...)
        const juce::String ss(m_proc.syx().status());
        if (ss != m_pmLastSyx) { m_pmLastSyx = ss; pmStatus("Synth: " + ss); }
        if (m_pmPendingRow >= 0 && juce::int32(juce::Time::getMillisecondCounter() - m_pmPendingAt) >= 120 && !m_proc.syx().busy()) {
            const int row = m_pmPendingRow;
            m_pmPendingRow = -1;
            pmLoadProgram(row);
        }
    }
    if (m_tab == 1) {
        const juce::String ss(m_proc.syx().status());
        if (ss != m_syxStatus.getText()) m_syxStatus.setText(ss, juce::dontSendNotification);
        const juce::String st = juce::String("MS2000R v" MS2K_VERSION "\n") + m_proc.status() + juce::String::formatted("\npanel draw %.1f ms", m_paintMs);
        if (st != m_status.getText()) m_status.setText(st, juce::dontSendNotification);
    }
    if (m_tab == 0 && refreshKnobs()) changed = true;
    if (changed) { m_dirty = false; repaint(); }
}

// LCD-DOTS-1 (the OB-12 plug-in's LCD technique): per device column (or row), which grid cells it overlaps and
// by how much of a dot. A dot is 0.86 of the pitch, the rest is the gap after it.
static void dotCover(int pixels, int cells, std::vector<int>& first, std::vector<float>& cover)
{
    const float pitch = float(pixels) / float(cells), dot = 0.86f;
    first.assign(size_t(pixels), 0);
    cover.assign(size_t(pixels) * 3, 0.0f);
    for (int p = 0; p < pixels; ++p) {
        const int c0 = juce::jmin(cells - 1, int(float(p) / pitch));
        first[size_t(p)] = c0;
        for (int k = 0; k < 3 && c0 + k < cells; ++k) {
            const float a = float(c0 + k) * pitch, b = a + dot * pitch;
            const float lo = juce::jmax(a, float(p)), hi = juce::jmin(b, float(p) + 1.0f);
            cover[size_t(p) * 3 + size_t(k)] = juce::jmax(0.0f, hi - lo);
        }
    }
}

// the backlit LCD glass: 16 x 2 cells of 5 x 8 dots fitted into r. LCD-DOTS-1: the dots are one grid of 95 x 17
// (a cell is 6 x 9 pitches, the sixth column and the ninth row hold no dot), computed at the exact device size and
// drawn 1:1 - no column is doubled by scaling, every dot is the same size with a gap at any window size. A device
// pixel's colour is the area it covers of gap, unlit dot and lit dot. Remade only when the dots or the size change.
void Ms2kEditor::drawLcd(juce::Graphics& g, juce::Rectangle<float> r)
{
    // the unit's yellow-green backlight with dark dots (Tamas' photo of the unit); R G B
    const float gap[3] = { 186, 220, 64 }, off[3] = { 172, 206, 58 }, on[3] = { 26, 38, 16 };
    constexpr int kCols = 16 * 6 - 1, kRows = 2 * 9 - 1;
    g.setColour(juce::Colour(186, 220, 64)); g.fillRoundedRectangle(r, 2.0f);
    const float ps = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const float pitch = juce::jmin(r.getWidth() / 101.0f, r.getHeight() / 23.0f) * ps;   // device pixels
    const int x0 = juce::roundToInt(r.getCentreX() * ps - 47.5f * pitch), y0 = juce::roundToInt(r.getCentreY() * ps - 8.5f * pitch);
    const int w = juce::roundToInt(kCols * pitch), h = juce::roundToInt(kRows * pitch);
    if (w < kCols || h < kRows) return;
    std::vector<uint8_t> dots(size_t(kCols * kRows), 0);
    for (int ln = 0; ln < 2; ++ln)
        for (int c = 0; c < 16; ++c) {
            const uint8_t ch = uint8_t((ln ? m_lcd.line1 : m_lcd.line0)[c]);
            for (int y = 0; y < 8; ++y) {
                const uint8_t bits = !m_lcd.displayOn ? 0 : (ch < 16 ? m_lcd.cgram[ch & 7][y] : (m_cgOk ? m_cg[ch][y] : 0));
                for (int x = 0; x < 5; ++x) dots[size_t((ln * 9 + y) * kCols + c * 6 + x)] = ((bits >> (4 - x)) & 1) ? 2 : 1;
            }
        }
    if (!m_lcdImg.isValid() || m_lcdImg.getWidth() != w || m_lcdImg.getHeight() != h || dots != m_lcdShown) {
        if (!m_lcdImg.isValid() || m_lcdImg.getWidth() != w || m_lcdImg.getHeight() != h) m_lcdImg = juce::Image(juce::Image::ARGB, w, h, false);
        m_lcdShown = dots;
        std::vector<int> fx, fy; std::vector<float> cx, cy;
        dotCover(w, kCols, fx, cx);
        dotCover(h, kRows, fy, cy);
        juce::Image::BitmapData bd(m_lcdImg, juce::Image::BitmapData::writeOnly);
        for (int py = 0; py < h; ++py) {
            uint8_t* line = bd.getLinePointer(py);
            for (int px = 0; px < w; ++px) {
                float lit = 0.0f, unlit = 0.0f;
                for (int j = 0; j < 3; ++j) {
                    const float wy = cy[size_t(py) * 3 + size_t(j)];
                    if (wy <= 0.0f) continue;
                    const int row = fy[size_t(py)] + j;
                    for (int i = 0; i < 3; ++i) {
                        const float wx = cx[size_t(px) * 3 + size_t(i)];
                        if (wx <= 0.0f) continue;
                        const uint8_t d = dots[size_t(row * kCols + fx[size_t(px)] + i)];
                        if (d == 2) lit += wx * wy; else if (d == 1) unlit += wx * wy;   // no dot between cells: gap
                    }
                }
                const float rest = juce::jmax(0.0f, 1.0f - lit - unlit);
                juce::uint8 v[3];
                for (int k = 0; k < 3; ++k) v[k] = juce::uint8(juce::jmin(255.0f, gap[k] * rest + off[k] * unlit + on[k] * lit + 0.5f));
                reinterpret_cast<juce::PixelARGB*>(line + px * bd.pixelStride)->setARGB(255, v[0], v[1], v[2]);
            }
        }
    }
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);   // nearest neighbour; the transform is 1:1 anyway
        g.drawImageTransformed(m_lcdImg, juce::AffineTransform::translation(float(x0), float(y0)).scaled(1.0f / ps));
    }
    if (!m_cgOk && m_lcd.displayOn) {   // no CG ROM: the characters as text over the unlit dots
        const float dot = pitch / ps, ox = float(x0) / ps, oy = float(y0) / ps;
        g.setColour(juce::Colour(26, 38, 16)); g.setFont(juce::Font(8.0f * dot, juce::Font::bold));
        for (int ln = 0; ln < 2; ++ln)
            for (int c = 0; c < 16; ++c) {
                const uint8_t ch = uint8_t((ln ? m_lcd.line1 : m_lcd.line0)[c]);
                if (ch >= 0x20 && ch < 0x7F)
                    g.drawText(juce::String::charToString(juce::juce_wchar(ch)), juce::Rectangle<float>(ox + c * 6 * dot, oy + ln * 9 * dot, 5 * dot, 8 * dot),
                               juce::Justification::centred, false);
            }
    }
}

void Ms2kEditor::paint(juce::Graphics& g)
{
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    g.fillAll(kBg);
    if (m_tab != 0) { g.setColour(col(VPanel::cPanel())); g.fillRect(getLocalBounds().withTrimmedTop(kTabH)); return; }
    const auto pa = panelArea();
    // the printed panel as a picture at the display's pixel scale, redrawn only when the size changes
    const float ps = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int iw = juce::roundToInt(float(getWidth()) * ps), ih = juce::roundToInt(float(getHeight()) * ps);
    if (!m_static.isValid() || m_static.getWidth() != iw || m_static.getHeight() != ih) {
        m_static = juce::Image(juce::Image::ARGB, iw, ih, true);
        juce::Graphics sg(m_static);
        sg.addTransform(juce::AffineTransform::scale(ps));
        DrawBackend sb(sg, m_mouse, m_face); sb.pass = 0;   // the same passive items as the draw pass: no key changes
        VPanel::draw(m_io, sb, VPanel::V2(pa.getX(), pa.getY()), VPanel::V2(pa.getWidth(), pa.getHeight()));
        sb.flush();
    }
    g.drawImageTransformed(m_static, juce::AffineTransform::scale(1.0f / ps));
    DrawBackend be(g, m_mouse, m_face); be.pass = 1;
    be.lcdFn = [this, &g](juce::Rectangle<float> r) { drawLcd(g, r); };
    VPanel::draw(m_io, be, VPanel::V2(pa.getX(), pa.getY()), VPanel::V2(pa.getWidth(), pa.getHeight()));
    be.flush();
    if (be.tip.isNotEmpty() && m_mouse.inside) {   // the knob's value
        const juce::Font f(14.0f);
        const float w = f.getStringWidthFloat(be.tip) + 12.0f;
        const juce::Rectangle<float> r(m_mouse.pos.x + 14.0f, m_mouse.pos.y + 16.0f, w, 20.0f);
        g.setColour(juce::Colour(20, 22, 26).withAlpha(0.92f)); g.fillRoundedRectangle(r, 3.0f);
        g.setColour(juce::Colour(236, 242, 244)); g.setFont(f); g.drawText(be.tip, r, juce::Justification::centred, false);
    }
    if (!m_proc.runner() && !m_proc.poweredOff()) {   // HOME-1 (PWR-SW-1: switched off = just a dark panel): not powered on - say why and where to fix it, over the panel
        const auto box = pa.withSizeKeepingCentre(pa.getWidth() * 0.62f, pa.getHeight() * 0.22f);
        g.setColour(juce::Colour(20, 22, 26).withAlpha(0.92f)); g.fillRoundedRectangle(box, 8.0f);
        g.setColour(juce::Colour(236, 242, 244)); g.setFont(juce::Font(juce::jmax(13.0f, pa.getHeight() * 0.024f)));
        g.drawFittedText(m_proc.status() + "\n\nSettings -> \"Choose the MS2000 folder...\" (the folder with flash.bin; boot-362.bin beside it or in its 'full FW' subfolder; hd44780_a00.bin optional).",
                         box.reduced(18.0f).toNearestInt(), juce::Justification::centred, 6);
    }
    m_paintMs = juce::Time::getMillisecondCounterHiRes() - t0;
}

// ---- PATCHES-1: the Patches page ----
// One page for what used to be Library and Bank. Left: the tree - "User" (the user banks, files in
// Documents/TimeZero/ms2k/patchmanager) and "Data Sources" (.syx / .mid files you list, read once and cached).
// Right: the programs of the selected bank. A program is only ever data until it is clicked: clicking sends it into
// the synth's edit buffer (CURRENT PROGRAM DATA DUMP, as before); the synth's own memory is not touched until WRITE
// on the synth, or until an exported .syx is loaded into it. "Copy Current Patch to Clipboard" (right click on the
// panel) and "Paste from Clipboard" move one program as text (MS2000::PresetBank::toClipboardText).
namespace {
const juce::Colour kAmber(240, 170, 60);
const juce::Colour kCyan(0, 224, 255);
const juce::Colour kText(230, 235, 238);
const juce::Colour kDim(130, 145, 152);

juce::File toFile(const std::filesystem::path& p) { return juce::File(juce::String::fromUTF8(MS2000::pathToUtf8(p).c_str())); }
std::filesystem::path toPath(const juce::File& f) { return MS2000::pathFromUtf8(f.getFullPathName().toStdString()); }
juce::String utf8(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }
// "C:\..\file.syx" - the drive and the file name, as the tree shows a Data Source
juce::String shortPath(const juce::File& f)
{
    const auto parent = f.getParentDirectory();
    if (parent == f || parent.getParentDirectory() == parent) return f.getFullPathName();           // lies in the root
    const juce::String sep = juce::File::getSeparatorString();
    return f.getFullPathName().upToFirstOccurrenceOf(sep, true, false) + ".." + sep + f.getFileName();
}
}

// ---- the tree ----
std::vector<Ms2kPatchTree::Row> Ms2kPatchTree::rows() const
{
    std::vector<Row> r;
    r.push_back({ UserHeader, -1 });
    if (m_userOpen) for (int i = 0; i < int(users.size()); ++i) r.push_back({ User, i });
    r.push_back({ SourceHeader, -1 });
    if (m_srcOpen) for (int i = 0; i < int(sources.size()); ++i) r.push_back({ Source, i });
    return r;
}

int Ms2kPatchTree::rowOf(Kind k, int i) const
{
    const auto r = rows();
    for (int n = 0; n < int(r.size()); ++n) if (r[size_t(n)].kind == k && r[size_t(n)].index == i) return n;
    return -1;
}

void Ms2kPatchTree::updateSize(int viewW, int viewH)
{
    m_viewW = viewW; m_viewH = viewH;
    setSize(juce::jmax(60, viewW), juce::jmax(viewH, int(rows().size()) * kRowH + 4));
}

void Ms2kPatchTree::scrollToItem(Kind k, int i)
{
    auto* vp = findParentComponentOfClass<juce::Viewport>();
    const int r = rowOf(k, i);
    if (!vp || r < 0) return;
    const int top = r * kRowH, vy = vp->getViewPositionY(), vh = vp->getMaximumVisibleHeight();
    if (top < vy) vp->setViewPosition(vp->getViewPositionX(), top);
    else if (top + kRowH > vy + vh) vp->setViewPosition(vp->getViewPositionX(), top + kRowH - vh);
}

juce::Rectangle<int> Ms2kPatchTree::iconRect(int row, Kind headerKind) const
{
    const juce::Font f(15.0f), fb(15.0f, juce::Font::bold | juce::Font::italic);
    const bool user = headerKind == UserHeader;
    const float x = 22.0f + f.getStringWidthFloat(user ? "User" : "Data Sources") + 6.0f
                    + fb.getStringWidthFloat("(" + juce::String(user ? userTotal : sourceTotal) + ")") + 10.0f;
    return { int(x), row * kRowH + 3, 18, 18 };
}

void Ms2kPatchTree::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(20, 24, 27));
    const auto r = rows();
    const juce::Font f(15.0f), fb(15.0f, juce::Font::bold | juce::Font::italic);
    for (int k = 0; k < int(r.size()); ++k) {
        const int y = k * kRowH;
        const Row row = r[size_t(k)];
        const bool hdr = row.kind == UserHeader || row.kind == SourceHeader;
        if (!hdr && row.kind == selKind && row.index == selIndex) { g.setColour(juce::Colour(88, 96, 102)); g.fillRect(0, y, getWidth(), kRowH); }
        juce::String name; int count = 0; bool missing = false;
        if (row.kind == UserHeader) { name = "User"; count = userTotal; }
        else if (row.kind == SourceHeader) { name = "Data Sources"; count = sourceTotal; }
        else {
            const auto& it = row.kind == User ? users[size_t(row.index)] : sources[size_t(row.index)];
            name = it.name; count = it.count; missing = it.missing;
        }
        const float x0 = hdr ? 22.0f : 26.0f;
        if (hdr) {
            const bool open = row.kind == UserHeader ? m_userOpen : m_srcOpen;
            juce::Path t;
            if (open) t.addTriangle(6.0f, float(y) + 9.0f, 16.0f, float(y) + 9.0f, 11.0f, float(y) + 16.0f);
            else t.addTriangle(8.0f, float(y) + 6.0f, 8.0f, float(y) + 18.0f, 15.0f, float(y) + 12.0f);
            g.setColour(kText); g.fillPath(t);
        }
        const float maxName = juce::jmax(10.0f, float(getWidth()) - x0 - 70.0f);
        const float nw = juce::jmin(f.getStringWidthFloat(name), maxName);
        const bool editing = m_ed && row.kind == User && row.index == m_edIndex;
        if (!editing) { g.setFont(f); g.setColour(missing ? kDim : kText); g.drawText(name, juce::Rectangle<float>(x0, float(y), nw, float(kRowH)), juce::Justification::centredLeft, true); }
        g.setFont(fb); g.setColour(missing ? kCyan.withAlpha(0.5f) : kCyan);
        if (!editing) g.drawText("(" + juce::String(count) + ")", juce::Rectangle<float>(x0 + nw + 6.0f, float(y), 70.0f, float(kRowH)), juce::Justification::centredLeft, false);
        if (hdr) {
            const auto ic = iconRect(k, row.kind).toFloat();
            g.setColour(kText);
            if (row.kind == UserHeader) {                           // the "+"
                g.drawEllipse(ic.reduced(1.5f), 1.6f);
                g.drawLine(ic.getCentreX() - 4.5f, ic.getCentreY(), ic.getCentreX() + 4.5f, ic.getCentreY(), 1.8f);
                g.drawLine(ic.getCentreX(), ic.getCentreY() - 4.5f, ic.getCentreX(), ic.getCentreY() + 4.5f, 1.8f);
            } else {                                                // the folder
                g.fillRect(ic.getX() + 1.0f, ic.getY() + 3.0f, 7.0f, 4.0f);
                g.drawRect(ic.getX() + 1.0f, ic.getY() + 6.0f, 16.0f, 10.0f, 1.6f);
            }
        }
    }
    g.setColour(juce::Colour(58, 66, 72)); g.fillRect(getWidth() - 1, 0, 1, getHeight());
}

void Ms2kPatchTree::mouseDown(const juce::MouseEvent& e)
{
    endRename(true);
    const auto r = rows();
    const int k = e.y < 0 ? -1 : e.y / kRowH;
    if (k < 0 || k >= int(r.size())) return;
    const Row row = r[size_t(k)];
    const bool hdr = row.kind == UserHeader || row.kind == SourceHeader;
    if (e.mods.isPopupMenu()) {
        if (!hdr) { selKind = row.kind; selIndex = row.index; repaint(); if (onSelect) onSelect(row.kind, row.index); }
        if (onContext) onContext(row.kind, row.index);
        return;
    }
    if (hdr) {
        if (iconRect(k, row.kind).expanded(3).contains(e.getPosition())) {
            if (row.kind == UserHeader) { if (onAddBank) onAddBank(); }
            else if (onAddSources) onAddSources();
            return;
        }
        if (row.kind == UserHeader) m_userOpen = !m_userOpen; else m_srcOpen = !m_srcOpen;
        relayout();
        repaint();
        return;
    }
    selKind = row.kind; selIndex = row.index;
    repaint();
    if (onSelect) onSelect(row.kind, row.index);
}

void Ms2kPatchTree::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto r = rows();
    const int k = e.y < 0 ? -1 : e.y / kRowH;
    if (k >= 0 && k < int(r.size()) && r[size_t(k)].kind == User && !e.mods.isPopupMenu()) beginRename(r[size_t(k)].index);
}

void Ms2kPatchTree::beginRename(int ui)
{
    if (ui < 0 || ui >= int(users.size())) return;
    endRename(false);
    m_userOpen = true;
    relayout();
    scrollToItem(User, ui);
    m_edIndex = ui;
    m_ed = std::make_unique<juce::TextEditor>();
    m_ed->setBounds(22, (ui + 1) * kRowH + 1, juce::jmax(100, getWidth() - 30), kRowH - 2);
    m_ed->setFont(juce::Font(15.0f));
    m_ed->setInputRestrictions(60);                                  // a bank name is a file name
    m_ed->setText(users[size_t(ui)].name, false);
    m_ed->selectAll();
    m_ed->onReturnKey = [this] { endRename(true); };
    m_ed->onEscapeKey = [this] { endRename(false); };
    m_ed->onFocusLost = [this] { endRename(true); };
    addAndMakeVisible(*m_ed);
    m_ed->grabKeyboardFocus();
    repaint();
}

void Ms2kPatchTree::endRename(bool commit)
{
    if (!m_ed) return;
    const juce::String text = m_ed->getText();
    const int idx = m_edIndex;
    std::shared_ptr<juce::TextEditor> old(std::move(m_ed));          // m_ed is empty before the child goes: no re-entry
    m_edIndex = -1;
    removeChildComponent(old.get());
    juce::MessageManager::callAsync([old] {});                       // it may be inside its own callback: destroy it later
    if (commit && onRenamed) onRenamed(idx, text);
    repaint();
}

// ---- the programs ----
void Ms2kPatchGrid::setNames(std::vector<juce::String> n)
{
    names = std::move(n);
    if (selected >= int(names.size())) selected = -1;
    if (active >= int(names.size())) active = -1;
    updateSize(m_viewW, m_viewH);
    repaint();
}

void Ms2kPatchGrid::updateSize(int viewW, int viewH)
{
    m_viewW = viewW; m_viewH = viewH;
    const int rpc = rowsPerCol();
    const int cols = names.empty() ? 1 : (int(names.size()) + rpc - 1) / rpc;
    setSize(juce::jmax(viewW, cols * kColW + 8), juce::jmax(60, viewH));
}

juce::Rectangle<int> Ms2kPatchGrid::cellRect(int index) const
{
    const int rpc = rowsPerCol();
    return { (index / rpc) * kColW, kTop + (index % rpc) * kRowH, kColW, kRowH };
}

int Ms2kPatchGrid::cellAt(juce::Point<int> p) const
{
    if (p.x < 0 || p.y < kTop) return -1;
    const int rpc = rowsPerCol();
    const int r = (p.y - kTop) / kRowH;
    if (r >= rpc) return -1;
    const int idx = (p.x / kColW) * rpc + r;
    return idx < int(names.size()) ? idx : -1;
}

void Ms2kPatchGrid::scrollToRow(int row)
{
    auto* vp = findParentComponentOfClass<juce::Viewport>();
    if (!vp || row < 0 || row >= int(names.size())) return;
    const int x = cellRect(row).getX(), vx = vp->getViewPositionX(), vw = vp->getMaximumVisibleWidth();
    if (x < vx) vp->setViewPosition(x, vp->getViewPositionY());
    else if (x + kColW > vx + vw) vp->setViewPosition(x + kColW - vw, vp->getViewPositionY());
}

void Ms2kPatchGrid::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(27, 31, 34));
    if (names.empty()) {
        g.setColour(kDim); g.setFont(juce::Font(15.0f));
        g.drawFittedText(emptyText, juce::Rectangle<int>(0, 0, juce::jmax(200, m_viewW), juce::jmax(60, m_viewH)).reduced(30), juce::Justification::centred, 5);
        return;
    }
    const auto clip = g.getClipBounds();
    const int rpc = rowsPerCol();
    const int c0 = juce::jmax(0, clip.getX() / kColW), c1 = clip.getRight() / kColW;
    g.setFont(juce::Font(15.0f));
    for (int c = c0; c <= c1; ++c) {
        for (int r = 0; r < rpc; ++r) {
            const int i = c * rpc + r;
            if (i >= int(names.size())) break;
            const auto cell = cellRect(i);
            const float alpha = (m_dragging && i == m_dragFrom) ? 0.35f : 1.0f;
            if (i == selected) { g.setColour(juce::Colour(70, 80, 87)); g.fillRect(cell.reduced(1, 0)); }
            if (i == active) { g.setColour(kAmber.withAlpha(alpha)); g.fillEllipse(float(cell.getX()) + 5.0f, float(cell.getCentreY()) - 3.0f, 6.0f, 6.0f); }
            g.setColour((i == selected ? juce::Colours::white : kText).withAlpha(alpha));
            g.drawText(names[size_t(i)], cell.getX() + 16, cell.getY(), kColW - 18, kRowH, juce::Justification::centredLeft, true);
        }
    }
    if (m_dragging && m_gap >= 0) { g.setColour(kAmber); g.fillRect(m_gapCol * kColW, kTop + m_gapSlot * kRowH - 1, kColW, 3); }
}

void Ms2kPatchGrid::mouseDown(const juce::MouseEvent& e)
{
    endRename(true);
    grabKeyboardFocus();                    // the arrow keys work after a click
    m_popup = e.mods.isPopupMenu();
    const int c = cellAt(e.getPosition());
    if (m_popup) {
        if (c >= 0) { selected = c; repaint(); }
        if (onContext) onContext(c);
        return;
    }
    if (c < 0) return;
    selected = c; m_dragFrom = c; m_dragging = false; m_gap = -1; m_downX = e.x; m_downY = e.y;
    beginDragAutoRepeat(40);                // keeps the autoscroll going while the mouse rests at an edge
    repaint();
}

void Ms2kPatchGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (m_popup || m_dragFrom < 0 || !editable || names.empty()) return;
    if (!m_dragging && std::abs(e.x - m_downX) < 5 && std::abs(e.y - m_downY) < 5) return;
    m_dragging = true;
    const int n = int(names.size()), rpc = rowsPerCol();
    const int col = juce::jlimit(0, (n - 1) / rpc, e.x / kColW);
    const int slot = juce::jlimit(0, juce::jmin(rpc, n - col * rpc), (e.y - kTop + kRowH / 2) / kRowH);
    m_gapCol = col; m_gapSlot = slot; m_gap = col * rpc + slot;
    if (auto* vp = findParentComponentOfClass<juce::Viewport>()) {
        const int vx = e.getEventRelativeTo(vp).x, vw = vp->getMaximumVisibleWidth();
        if (vx < 24) vp->setViewPosition(juce::jmax(0, vp->getViewPositionX() - 16), vp->getViewPositionY());
        else if (vx > vw - 24) vp->setViewPosition(vp->getViewPositionX() + 16, vp->getViewPositionY());
    }
    repaint();
}

void Ms2kPatchGrid::mouseUp(const juce::MouseEvent& e)
{
    if (m_popup) { m_popup = false; return; }
    if (m_dragFrom < 0) return;
    const int from = m_dragFrom, gap = m_gap;
    const bool dragged = m_dragging;
    m_dragFrom = -1; m_gap = -1; m_dragging = false;
    repaint();
    if (dragged) { if (gap >= 0 && onMove) onMove(from, gap); }
    // The second click of a double-click does not send the program again: the first click did, and the second would
    // only meet the transfer still running.
    else if (e.getNumberOfClicks() < 2 && cellAt(e.getPosition()) == from && onLoad) onLoad(from);
}

bool Ms2kPatchGrid::keyPressed(const juce::KeyPress& k)
{
    if (names.empty() || m_ed) return false;                          // while a name is typed the arrows belong to the text
    if (k == juce::KeyPress::upKey) { if (onStep) onStep(-1); return true; }
    if (k == juce::KeyPress::downKey) { if (onStep) onStep(1); return true; }
    return false;
}

void Ms2kPatchGrid::beginRename(int row)
{
    if (!editable || row < 0 || row >= int(names.size())) return;
    endRename(false);
    selected = row; m_edRow = row;
    const auto rc = cellRect(row);
    m_ed = std::make_unique<juce::TextEditor>();
    m_ed->setBounds(rc.getX(), rc.getY(), juce::jmax(kColW, juce::jmin(kColW + 40, getWidth() - rc.getX())), kRowH);
    m_ed->setFont(juce::Font(15.0f));
    m_ed->setInputRestrictions(int(MS2000::kBankNameChars));        // the program name is 12 characters
    m_ed->setText(names[size_t(row)], false);
    m_ed->selectAll();
    m_ed->onReturnKey = [this] { endRename(true); };
    m_ed->onEscapeKey = [this] { endRename(false); };
    m_ed->onFocusLost = [this] { endRename(true); };
    addAndMakeVisible(*m_ed);
    m_ed->grabKeyboardFocus();
    repaint();
}

void Ms2kPatchGrid::endRename(bool commit)
{
    if (!m_ed) return;
    const juce::String text = m_ed->getText();
    const int row = m_edRow;
    std::shared_ptr<juce::TextEditor> old(std::move(m_ed));
    m_edRow = -1;
    removeChildComponent(old.get());
    juce::MessageManager::callAsync([old] {});
    if (commit && onRenamed) onRenamed(row, text);
    repaint();
}

// ---- the page ----
void Ms2kEditor::toast(const juce::String& s)
{
    m_toast.setText(s, juce::dontSendNotification);
    m_toastUntil = juce::Time::getMillisecondCounter() + 3500; if (m_toastUntil == 0) m_toastUntil = 1;
}

bool Ms2kEditor::clipboardHasPatch() const
{
    std::vector<uint8_t> p;
    return MS2000::PresetBank::fromClipboardText(juce::SystemClipboard::getTextFromClipboard().toStdString(), p);
}

void Ms2kEditor::pmRebuildTree()
{
    auto& pm = m_proc.patches();
    m_tree.users.clear(); m_tree.sources.clear();
    for (size_t i = 0; i < pm.userCount(); ++i) m_tree.users.push_back({ utf8(pm.user(i).name), int(pm.user(i).bank.size()), false });
    for (size_t i = 0; i < pm.sourceCount(); ++i) m_tree.sources.push_back({ shortPath(toFile(pm.source(i).path)), int(pm.source(i).count), pm.source(i).missing });
    m_tree.userTotal = int(pm.userTotal()); m_tree.sourceTotal = int(pm.sourceTotal());
    if (m_tree.selKind == Ms2kPatchTree::User && m_tree.selIndex >= int(pm.userCount())) { m_tree.selKind = Ms2kPatchTree::None; m_tree.selIndex = -1; }
    if (m_tree.selKind == Ms2kPatchTree::Source && m_tree.selIndex >= int(pm.sourceCount())) { m_tree.selKind = Ms2kPatchTree::None; m_tree.selIndex = -1; }
    m_tree.relayout();                                                // the number of rows changed
    m_tree.repaint();
}

void Ms2kEditor::pmShow()
{
    auto& pm = m_proc.patches();
    std::vector<juce::String> names;
    m_grid.editable = false; m_grid.active = -1;
    if (m_tree.selKind == Ms2kPatchTree::User && m_tree.selIndex >= 0 && m_tree.selIndex < int(pm.userCount())) {
        const auto& u = pm.user(size_t(m_tree.selIndex));
        m_grid.editable = true;
        for (size_t i = 0; i < u.bank.size(); ++i) names.push_back(utf8(u.bank.name(i)));
        m_grid.emptyText = "This bank is empty.\nRight-click here: Add Current Patch / Paste from Clipboard.";
        if (m_activeBank == MS2000::pathToUtf8(u.file)) m_grid.active = u.bank.indexOfId(m_activeEntry);
    } else if (m_tree.selKind == Ms2kPatchTree::Source && m_tree.selIndex >= 0 && m_tree.selIndex < int(pm.sourceCount())) {
        std::string err;
        const bool ok = pm.loadSource(size_t(m_tree.selIndex), err);
        const auto& s = pm.source(size_t(m_tree.selIndex));
        if (ok) {
            for (const auto& p : s.progs) names.push_back(utf8(MS2000::PresetBank::nameOf(p.data())));
            if (m_activeBank == MS2000::pathToUtf8(s.path) && m_activeEntry >= 1 && m_activeEntry <= names.size()) m_grid.active = int(m_activeEntry) - 1;
            m_grid.emptyText = "";
        } else m_grid.emptyText = utf8(err) + ":\n" + toFile(s.path).getFullPathName();
        pmRebuildTree();                                    // reading the source may have changed its count / missing flag
    } else m_grid.emptyText = "Select a bank on the left.";
    m_grid.setNames(std::move(names));
}

// Up / Down: the highlight moves at once; the program is sent when the keys rest (a held arrow would otherwise
// start a transfer per step and meet the previous one still running).
void Ms2kEditor::pmStep(int delta)
{
    const int n = int(m_grid.names.size());
    if (n == 0) return;
    const int row = m_grid.selected < 0 ? (delta > 0 ? 0 : n - 1) : juce::jlimit(0, n - 1, m_grid.selected + delta);
    m_grid.selected = row;
    m_grid.scrollToRow(row);
    m_grid.repaint();
    m_pmPendingRow = row;
    m_pmPendingAt = juce::Time::getMillisecondCounter();
}

void Ms2kEditor::pmSelect(Ms2kPatchTree::Kind k, int i)
{
    m_pmPendingRow = -1;
    m_tree.selKind = k; m_tree.selIndex = i;
    m_grid.selected = -1;
    pmShow();
    m_gridView.setViewPosition(0, 0);
    m_tree.scrollToItem(k, i);                                        // a bank added or selected below the visible rows
    m_tree.repaint();
    m_grid.grabKeyboardFocus();                                       // the arrows step through the programs just shown
    auto& pm = m_proc.patches();
    if (k == Ms2kPatchTree::Source && i >= 0 && i < int(pm.sourceCount())) pmStatus(toFile(pm.source(size_t(i)).path).getFullPathName());
    else if (k == Ms2kPatchTree::User && i >= 0 && i < int(pm.userCount())) pmStatus(toFile(pm.user(size_t(i)).file).getFullPathName());
}

void Ms2kEditor::pmSyncDisk()
{
    auto& pm = m_proc.patches();
    std::filesystem::path selPath;
    Ms2kPatchTree::Kind sk = m_tree.selKind;
    if (sk == Ms2kPatchTree::User && m_tree.selIndex >= 0 && m_tree.selIndex < int(pm.userCount())) selPath = pm.user(size_t(m_tree.selIndex)).file;
    else if (sk == Ms2kPatchTree::Source && m_tree.selIndex >= 0 && m_tree.selIndex < int(pm.sourceCount())) selPath = pm.source(size_t(m_tree.selIndex)).path;
    else sk = Ms2kPatchTree::None;
    pm.load();                                              // other plug-in instances may have changed the files
    int ni = -1;
    if (sk == Ms2kPatchTree::User) ni = pm.indexOfUserFile(selPath);
    else if (sk == Ms2kPatchTree::Source) ni = pm.indexOfSourcePath(selPath);
    m_tree.selKind = ni >= 0 ? sk : Ms2kPatchTree::None; m_tree.selIndex = ni;
    pmRebuildTree();
    pmShow();
}

bool Ms2kEditor::pmProgram(int row, std::vector<uint8_t>& data, std::string& name, std::string& key, uint64_t& entry)
{
    auto& pm = m_proc.patches();
    if (row < 0) return false;
    if (m_tree.selKind == Ms2kPatchTree::User && m_tree.selIndex >= 0 && m_tree.selIndex < int(pm.userCount())) {
        const auto& u = pm.user(size_t(m_tree.selIndex));
        if (row >= int(u.bank.size())) return false;
        data = u.bank.at(size_t(row)).data; name = u.bank.name(size_t(row)); key = MS2000::pathToUtf8(u.file); entry = u.bank.at(size_t(row)).id;
        return true;
    }
    if (m_tree.selKind == Ms2kPatchTree::Source && m_tree.selIndex >= 0 && m_tree.selIndex < int(pm.sourceCount())) {
        const auto& s = pm.source(size_t(m_tree.selIndex));
        if (!s.loaded || row >= int(s.progs.size())) return false;
        data = s.progs[size_t(row)]; name = MS2000::PresetBank::nameOf(data.data()); key = MS2000::pathToUtf8(s.path); entry = uint64_t(row) + 1;
        return true;
    }
    return false;
}

void Ms2kEditor::pmLoadProgram(int row)
{
    std::vector<uint8_t> data; std::string name, key; uint64_t entry = 0;
    if (!pmProgram(row, data, name, key, entry)) return;
    if (!m_proc.runner()) { pmStatus("No machine is running (POWER is off), so there is nothing to send to."); return; }
    if (!m_proc.syx().startProgram(data, name)) { pmStatus("Busy - wait for the transfer in progress"); return; }
    m_activeBank = key; m_activeEntry = entry; m_grid.active = row;
    m_grid.repaint();
    pmStatus("Sent \"" + utf8(name) + "\" to the synth's edit buffer (WRITE on the synth stores it in its memory).");
}

void Ms2kEditor::pmAddTo(int bank, const std::vector<uint8_t>& prog, const juce::String& what)
{
    auto& pm = m_proc.patches();
    if (bank < 0 || bank >= int(pm.userCount())) { pmStatus("Select a user bank first."); return; }
    auto& u = pm.user(size_t(bank));
    const int i = u.bank.add(prog);
    if (i < 0) { pmStatus("The bank \"" + utf8(u.name) + "\" is full (128 programs)."); return; }
    const bool saved = pm.saveBank(size_t(bank));
    pmRebuildTree();
    if (m_tree.selKind == Ms2kPatchTree::User && m_tree.selIndex == bank) { pmShow(); m_grid.selected = i; m_grid.scrollToRow(i); m_grid.repaint(); }
    pmStatus(what + " added to \"" + utf8(u.name) + "\" as no. " + juce::String(i + 1) + "." + (saved ? "" : "  (the bank file could not be written)"));
}

void Ms2kEditor::pmAddCurrent(int bank)
{
    std::vector<uint8_t> live;
    if (!m_proc.readEditBuffer(live)) { pmStatus("No sound to take: the machine is not running (POWER off)."); return; }
    pmAddTo(bank, live, "\"" + utf8(MS2000::PresetBank::nameOf(live.data())) + "\"");
}

void Ms2kEditor::pmPaste(int bank)
{
    std::vector<uint8_t> p;
    if (!MS2000::PresetBank::fromClipboardText(juce::SystemClipboard::getTextFromClipboard().toStdString(), p)) {
        pmStatus("The clipboard holds no MS2000R patch (use \"Copy Current Patch to Clipboard\" first).");
        return;
    }
    pmAddTo(bank, p, "\"" + utf8(MS2000::PresetBank::nameOf(p.data())) + "\" from the clipboard");
}

void Ms2kEditor::pmCopyRow(int row)
{
    std::vector<uint8_t> data; std::string name, key; uint64_t entry = 0;
    if (!pmProgram(row, data, name, key, entry)) return;
    juce::SystemClipboard::copyTextToClipboard(utf8(MS2000::PresetBank::toClipboardText(data)));
    pmStatus("Copied \"" + utf8(name) + "\" to the clipboard.");
    toast("Copied \"" + utf8(name) + "\"");
}

void Ms2kEditor::pmCopyCurrent()
{
    std::vector<uint8_t> live;
    if (!m_proc.readEditBuffer(live)) { toast("No sound to copy: the machine is not running"); return; }
    juce::SystemClipboard::copyTextToClipboard(utf8(MS2000::PresetBank::toClipboardText(live)));
    toast("Copied \"" + utf8(MS2000::PresetBank::nameOf(live.data())) + "\" to the clipboard");
}

void Ms2kEditor::pmOverwrite(int row)
{
    auto& pm = m_proc.patches();
    if (m_tree.selKind != Ms2kPatchTree::User || m_tree.selIndex < 0 || m_tree.selIndex >= int(pm.userCount())) return;
    auto& u = pm.user(size_t(m_tree.selIndex));
    if (row < 0 || row >= int(u.bank.size())) return;
    std::vector<uint8_t> live;
    if (!m_proc.readEditBuffer(live)) { pmStatus("No sound to take: the machine is not running (POWER off)."); return; }
    const juce::String was = utf8(u.bank.name(size_t(row)));
    u.bank.replace(size_t(row), live);
    pm.saveBank(size_t(m_tree.selIndex));
    m_activeBank = MS2000::pathToUtf8(u.file); m_activeEntry = u.bank.at(size_t(row)).id;
    pmShow();
    m_grid.selected = row;
    pmStatus("No. " + juce::String(row + 1) + " \"" + was + "\" now holds the current sound, named \"" + utf8(u.bank.name(size_t(row))) + "\".");
}

void Ms2kEditor::pmDeleteProgram(int row)
{
    auto& pm = m_proc.patches();
    if (m_tree.selKind != Ms2kPatchTree::User || m_tree.selIndex < 0 || m_tree.selIndex >= int(pm.userCount())) return;
    auto& u = pm.user(size_t(m_tree.selIndex));
    if (row < 0 || row >= int(u.bank.size())) return;
    const juce::String was = utf8(u.bank.name(size_t(row)));
    u.bank.remove(size_t(row));
    pm.saveBank(size_t(m_tree.selIndex));
    pmRebuildTree(); pmShow();
    m_grid.selected = u.bank.empty() ? -1 : juce::jmin(row, int(u.bank.size()) - 1);
    m_grid.repaint();
    pmStatus("Deleted \"" + was + "\" from \"" + utf8(u.name) + "\".");
}

void Ms2kEditor::pmRenameProgram(int row, const juce::String& text)
{
    auto& pm = m_proc.patches();
    if (m_tree.selKind != Ms2kPatchTree::User || m_tree.selIndex < 0 || m_tree.selIndex >= int(pm.userCount())) return;
    auto& u = pm.user(size_t(m_tree.selIndex));
    auto& b = u.bank;
    if (row < 0 || row >= int(b.size())) return;
    const std::string wanted = MS2000::PresetBank::sanitizeName(text.toStdString());
    if (wanted.find_first_not_of(' ') == std::string::npos) { pmStatus("A program name cannot be empty."); return; }
    const std::string oldName = b.name(size_t(row));
    b.rename(size_t(row), wanted);
    const std::string newName = b.name(size_t(row));
    if (newName == oldName) { pmStatus("The name is unchanged."); return; }
    pm.saveBank(size_t(m_tree.selIndex));
    juce::String msg = "Renamed \"" + utf8(oldName) + "\" to \"" + utf8(newName) + "\".";
    // The synth shows the name once the program is in its edit buffer. If this entry is the sound the synth holds
    // right now, send the synth's own current state with the new name (the knobs turned since stay); otherwise send
    // the stored program.
    if (m_proc.runner()) {
        std::vector<uint8_t> toSend = b.at(size_t(row)).data, live;
        bool keptKnobs = false;
        if (m_activeBank == MS2000::pathToUtf8(u.file) && b.at(size_t(row)).id == m_activeEntry && m_proc.readEditBuffer(live)
            && MS2000::PresetBank::nameOf(live.data()) == oldName) {
            toSend = MS2000::PresetBank::withName(live, newName); keptKnobs = true;
        }
        if (m_proc.syx().startProgram(toSend, newName)) {
            m_activeBank = MS2000::pathToUtf8(u.file); m_activeEntry = b.at(size_t(row)).id;
            msg += keptKnobs ? " The synth's current sound (knobs kept) carries the new name now." : " Sent to the synth, which now shows the new name.";
        } else msg += " The synth is busy, so it was not sent - click the program to send it.";
    } else msg += " (No machine running: the name shows once the program is sent.)";
    pmShow();
    m_grid.selected = row;
    pmStatus(msg);
}

void Ms2kEditor::pmMoveProgram(int row, int gap)
{
    auto& pm = m_proc.patches();
    if (m_tree.selKind != Ms2kPatchTree::User || m_tree.selIndex < 0 || m_tree.selIndex >= int(pm.userCount())) return;
    auto& u = pm.user(size_t(m_tree.selIndex));
    const int to = u.bank.moveToGap(size_t(row), size_t(gap));
    if (to < 0) return;
    pm.saveBank(size_t(m_tree.selIndex));
    pmShow();
    m_grid.selected = to; m_grid.scrollToRow(to); m_grid.repaint();
    pmStatus("Moved \"" + utf8(u.bank.name(size_t(to))) + "\" to no. " + juce::String(to + 1) + ".");
}

void Ms2kEditor::pmNewBank()
{
    auto& pm = m_proc.patches();
    const int i = pm.createBank("New Bank");
    if (i < 0) { pmStatus("Could not create the bank file in " + toFile(pm.root()).getFullPathName()); return; }
    pmRebuildTree();
    pmSelect(Ms2kPatchTree::User, i);
    pmStatus("New bank - type its name. Then right-click in the list: Add Current Patch / Paste from Clipboard.");
    juce::MessageManager::callAsync([this, i] { m_tree.beginRename(i); });
}

void Ms2kEditor::pmRenameBank(int bank, const juce::String& text)
{
    auto& pm = m_proc.patches();
    if (bank < 0 || bank >= int(pm.userCount())) return;
    const std::string oldKey = MS2000::pathToUtf8(pm.user(size_t(bank)).file);
    if (!pm.renameBank(size_t(bank), text.toStdString())) {
        pmRebuildTree();
        pmStatus("Could not rename: the name is empty or another bank or file already has it.");
        return;
    }
    if (m_activeBank == oldKey) m_activeBank = MS2000::pathToUtf8(pm.user(size_t(bank)).file);
    pmRebuildTree();
    pmStatus("Renamed to \"" + utf8(pm.user(size_t(bank)).name) + "\" (the file was renamed too).");
}

void Ms2kEditor::pmDeleteBank(int bank)
{
    auto& pm = m_proc.patches();
    if (bank < 0 || bank >= int(pm.userCount())) return;
    const juce::String name = utf8(pm.user(size_t(bank)).name);
    const std::filesystem::path file = pm.user(size_t(bank)).file;
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Delete bank",
        "Delete the bank \"" + name + "\" and its file?\nThis cannot be undone.", "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create([this, file, name](int result) {
            if (result != 1) return;
            auto& pm2 = m_proc.patches();
            const int i = pm2.indexOfUserFile(file);
            if (i < 0) return;
            if (!pm2.deleteBank(size_t(i))) { pmStatus("Could not delete the file of \"" + name + "\"."); return; }
            if (m_tree.selKind == Ms2kPatchTree::User) { if (m_tree.selIndex == i) { m_tree.selKind = Ms2kPatchTree::None; m_tree.selIndex = -1; } else if (m_tree.selIndex > i) --m_tree.selIndex; }
            pmRebuildTree(); pmShow();
            pmStatus("Deleted the bank \"" + name + "\" and its file.");
        }));
}

void Ms2kEditor::pmExport(int bank, bool midi)
{
    auto& pm = m_proc.patches();
    if (bank < 0 || bank >= int(pm.userCount())) return;
    if (pm.user(size_t(bank)).bank.empty()) { pmStatus("The bank is empty - nothing to export."); return; }
    const std::filesystem::path file = pm.user(size_t(bank)).file;
    const juce::File dir = m_proc.libraryPath.isNotEmpty() ? juce::File(m_proc.libraryPath).getParentDirectory()
                                                           : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    m_chooser = std::make_unique<juce::FileChooser>(midi ? "Export the bank as a MIDI file" : "Export the bank as a .syx",
                                                    dir.getChildFile(utf8(pm.user(size_t(bank)).name) + (midi ? ".mid" : ".syx")), midi ? "*.mid" : "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                           [this, midi, file](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (!f.hasFileExtension(midi ? "mid;midi" : "syx")) f = f.withFileExtension(midi ? "mid" : "syx");
        auto& pm2 = m_proc.patches();
        const int i = pm2.indexOfUserFile(file);
        if (i < 0) return;
        const auto& b = pm2.user(size_t(i)).bank;
        size_t fromSynth = 0;
        const MS2000::PresetBank::Filler fill = [this, &fromSynth](size_t slot, uint8_t* out) { if (!m_proc.machineProgram(slot, out)) return false; ++fromSynth; return true; };
        const auto data = midi ? b.toMidi(f.getFileNameWithoutExtension().toStdString(), fill) : b.toSyx(fill);
        if (data.empty() || !f.replaceWithData(data.data(), data.size())) { pmStatus("Could not write " + f.getFullPathName()); return; }
        m_proc.libraryPath = f.getFullPathName();
        const int n = int(b.size()), rest = 128 - n;
        juce::String s = "Exported " + f.getFileName() + ": " + juce::String(n) + " programs as slots 1-" + juce::String(n);
        if (rest > 0) {
            s += ", slots " + juce::String(n + 1) + "-128 filled with ";
            s += fromSynth > 0 ? "the synth's own programs of those slots" : "copies of the first program named \"-empty-\" (the synth is not running)";
            if (fromSynth > 0 && int(fromSynth) < rest) s += " (" + juce::String(rest - int(fromSynth)) + " of them as \"-empty-\")";
        }
        pmStatus(s + ". The unit loads a bank as a whole (all 128 slots).");
    });
}

void Ms2kEditor::pmImport(bool asBankFile)
{
    const juce::File dir = m_proc.libraryPath.isNotEmpty() ? juce::File(m_proc.libraryPath).getParentDirectory()
                                                           : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    m_chooser = std::make_unique<juce::FileChooser>(asBankFile ? "Bank files (.ms2kbank)" : "Make banks from .syx / .mid / .ms2kbank files", dir,
                                                    asBankFile ? "*.ms2kbank" : "*.syx;*.mid;*.midi;*.ms2kbank");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                           [this, asBankFile](const juce::FileChooser& fc) {
        auto& pm = m_proc.patches();
        int made = 0, last = -1;
        juce::String problem;
        for (const auto& f : fc.getResults()) {
            std::string err;
            if (asBankFile) { const int i = pm.addBankFile(toPath(f)); if (i >= 0) { ++made; last = i; } else problem = f.getFileName() + ": not a bank file"; }
            else {
                const auto idx = pm.importFile(toPath(f), err);
                for (int i : idx) { ++made; last = i; }
                if (idx.empty()) problem = f.getFileName() + ": " + utf8(err);
            }
        }
        if (fc.getResults().size() > 0) m_proc.libraryPath = fc.getResult().getFullPathName();
        pmRebuildTree();
        if (last >= 0) pmSelect(Ms2kPatchTree::User, last);
        pmStatus(made > 0 ? juce::String(made) + (asBankFile ? " bank(s) listed." : " bank(s) made.") + (problem.isNotEmpty() ? "  " + problem : juce::String())
                          : (problem.isNotEmpty() ? problem : juce::String("Nothing chosen.")));
    });
}

void Ms2kEditor::pmAddSources()
{
    const juce::File dir = m_proc.libraryPath.isNotEmpty() ? juce::File(m_proc.libraryPath).getParentDirectory()
                                                           : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    m_chooser = std::make_unique<juce::FileChooser>("Add .syx / .mid files with MS2000 programs", dir, "*.syx;*.mid;*.midi");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                           [this](const juce::FileChooser& fc) {
        auto& pm = m_proc.patches();
        int added = 0, last = -1;
        juce::String problem;
        for (const auto& f : fc.getResults()) {
            std::string err;
            const size_t before = pm.sourceCount();
            const int i = pm.addSource(toPath(f), err);
            if (i >= 0) { last = i; if (pm.sourceCount() > before) ++added; }
            else problem = f.getFileName() + ": " + utf8(err);
        }
        if (fc.getResults().size() > 0) m_proc.libraryPath = fc.getResult().getFullPathName();
        pmRebuildTree();
        if (last >= 0) pmSelect(Ms2kPatchTree::Source, last);
        pmStatus(juce::String(added) + " source(s) added." + (problem.isNotEmpty() ? "  " + problem : juce::String()));
    });
}

void Ms2kEditor::pmAddFolder()
{
    const juce::File dir = m_proc.libraryPath.isNotEmpty() ? juce::File(m_proc.libraryPath).getParentDirectory()
                                                           : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    m_chooser = std::make_unique<juce::FileChooser>("Add every .syx / .mid in a folder (and its subfolders)", dir);
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
        const auto d = fc.getResult();
        if (!d.isDirectory()) return;
        auto& pm = m_proc.patches();
        const size_t n = pm.addSourcesFromFolder(toPath(d));
        m_proc.libraryPath = d.getFullPathName();
        pmRebuildTree();
        pmStatus(juce::String(int(n)) + " source(s) added from " + d.getFullPathName() + ".");
    });
}

void Ms2kEditor::pmReveal(const juce::File& f)
{
    if (f.exists()) f.revealToUser();
    else if (f.getParentDirectory().isDirectory()) f.getParentDirectory().startAsProcess();
    else pmStatus("Neither the file nor its folder is there: " + f.getFullPathName());
}

void Ms2kEditor::pmTreeMenu(Ms2kPatchTree::Kind k, int i)
{
    auto& pm = m_proc.patches();
    juce::PopupMenu m;
    if (k == Ms2kPatchTree::UserHeader) {
        m.addItem("New Bank", [this] { pmNewBank(); });
        m.addItem("Import .syx / .mid...", [this] { pmImport(false); });
        m.addItem("Add Bank File...", [this] { pmImport(true); });
        m.addSeparator();
        m.addItem("Refresh", [this] { pmSyncDisk(); pmStatus("Banks read from disk again."); });
    } else if (k == Ms2kPatchTree::SourceHeader) {
        m.addItem("Add Files...", [this] { pmAddSources(); });
        m.addItem("Add Folder...", [this] { pmAddFolder(); });
        m.addSeparator();
        m.addItem("Refresh All", [this] {
            auto& p = m_proc.patches();
            int bad = 0;
            for (size_t s = 0; s < p.sourceCount(); ++s) { std::string err; if (!p.refreshSource(s, err)) ++bad; }
            pmRebuildTree(); pmShow();
            pmStatus("Data Sources read again" + juce::String(bad > 0 ? ", " + juce::String(bad) + " not readable." : juce::String(".")));
        });
    } else if (k == Ms2kPatchTree::User && i >= 0 && i < int(pm.userCount())) {
        const bool clip = clipboardHasPatch();
        const bool room = !pm.user(size_t(i)).bank.full();
        const std::filesystem::path file = pm.user(size_t(i)).file;
        auto idx = [this, file]() { return m_proc.patches().indexOfUserFile(file); };          // the list may change while the menu is open
        m.addItem("Refresh", [this, idx] {
            auto& p = m_proc.patches(); const int b = idx();
            if (b < 0) return;
            pmStatus(p.refreshBank(size_t(b)) ? "Read \"" + utf8(p.user(size_t(b)).name) + "\" from disk again." : juce::String("The file cannot be read."));
            pmRebuildTree(); pmShow();
        });
        m.addItem("Remove", [this, idx] {
            auto& p = m_proc.patches(); const int b = idx();
            if (b < 0) return;
            const juce::String name = utf8(p.user(size_t(b)).name);
            p.removeBank(size_t(b));
            if (m_tree.selKind == Ms2kPatchTree::User) { if (m_tree.selIndex == b) { m_tree.selKind = Ms2kPatchTree::None; m_tree.selIndex = -1; } else if (m_tree.selIndex > b) --m_tree.selIndex; }
            pmRebuildTree(); pmShow();
            pmStatus("\"" + name + "\" removed from the list (its file is still there).");
        });
        m.addItem("Reveal in File Browser", [this, file] { pmReveal(toFile(file)); });
        m.addItem("Delete...", [this, idx] { pmDeleteBank(idx()); });
        m.addItem("Rename", [this, idx] { const int b = idx(); juce::MessageManager::callAsync([this, b] { m_tree.beginRename(b); }); });
        m.addItem("Export as .syx...", [this, idx] { pmExport(idx(), false); });
        m.addItem("Export as .mid...", [this, idx] { pmExport(idx(), true); });
        m.addSeparator();
        m.addItem("Paste from Clipboard", clip && room, false, [this, idx] { pmPaste(idx()); });
        m.addItem("Add Current Patch", room, false, [this, idx] { pmAddCurrent(idx()); });
    } else if (k == Ms2kPatchTree::Source && i >= 0 && i < int(pm.sourceCount())) {
        const std::filesystem::path path = pm.source(size_t(i)).path;
        auto idx = [this, path]() { return m_proc.patches().indexOfSourcePath(path); };
        m.addItem("Refresh", [this, idx] {
            auto& p = m_proc.patches(); const int s = idx();
            if (s < 0) return;
            std::string err;
            const bool ok = p.refreshSource(size_t(s), err);
            pmRebuildTree(); pmShow();
            pmStatus(ok ? "Read " + toFile(p.source(size_t(s)).path).getFileName() + " again: " + juce::String(int(p.source(size_t(s)).count)) + " programs." : utf8(err) + ".");
        });
        m.addItem("Remove", [this, idx] {
            auto& p = m_proc.patches(); const int s = idx();
            if (s < 0) return;
            p.removeSource(size_t(s));
            if (m_tree.selKind == Ms2kPatchTree::Source) { if (m_tree.selIndex == s) { m_tree.selKind = Ms2kPatchTree::None; m_tree.selIndex = -1; } else if (m_tree.selIndex > s) --m_tree.selIndex; }
            pmRebuildTree(); pmShow();
            pmStatus("Removed from the list (the file itself is untouched).");
        });
        m.addItem("Reveal in File Browser", [this, path] { pmReveal(toFile(path)); });
    } else return;
    m.showMenuAsync(juce::PopupMenu::Options());
}

void Ms2kEditor::pmGridMenu(int row)
{
    auto& pm = m_proc.patches();
    const bool user = m_tree.selKind == Ms2kPatchTree::User && m_tree.selIndex >= 0 && m_tree.selIndex < int(pm.userCount());
    const bool src = m_tree.selKind == Ms2kPatchTree::Source && m_tree.selIndex >= 0 && m_tree.selIndex < int(pm.sourceCount());
    if (!user && !src) return;
    if (row < 0 && !user) return;                                     // nothing to offer on empty space of a Data Source
    const int bank = m_tree.selIndex;
    const bool clip = clipboardHasPatch();
    const bool room = user && !pm.user(size_t(bank)).bank.full();
    juce::PopupMenu m;
    if (row >= 0) {
        if (user) {
            m.addItem("Rename", [this, row] { juce::MessageManager::callAsync([this, row] { m_grid.beginRename(row); }); });
            m.addItem("Delete", [this, row] { pmDeleteProgram(row); });
            m.addItem("Overwrite with Current Patch", [this, row] { pmOverwrite(row); });
            m.addSeparator();
        }
        m.addItem("Copy Patch to Clipboard", [this, row] { pmCopyRow(row); });
        if (src) {
            juce::PopupMenu sub;
            for (size_t b = 0; b < pm.userCount(); ++b)
                sub.addItem(utf8(pm.user(b).name), !pm.user(b).bank.full(), false, [this, row, b] {
                    std::vector<uint8_t> data; std::string name, key; uint64_t entry = 0;
                    if (pmProgram(row, data, name, key, entry)) pmAddTo(int(b), data, "\"" + utf8(name) + "\"");
                });
            m.addSubMenu("Add to Bank", sub, pm.userCount() > 0);
        }
        if (user) m.addSeparator();
    }
    if (user) {
        m.addItem("Paste from Clipboard", clip && room, false, [this, bank] { pmPaste(bank); });
        m.addItem("Add Current Patch", room, false, [this, bank] { pmAddCurrent(bank); });
    }
    m.showMenuAsync(juce::PopupMenu::Options());
}

// The right click on the front panel: copy the sound the synth plays now (the knobs as they are). With a user bank
// selected on the Patches page the sound can go straight into it, and the clipboard's patch can be pasted into it.
void Ms2kEditor::pmPanelMenu()
{
    std::vector<uint8_t> live;
    const bool haveLive = m_proc.readEditBuffer(live);
    juce::PopupMenu m;
    m.addItem("Copy Current Patch to Clipboard", haveLive, false, [this] { pmCopyCurrent(); });
    if (m_tree.selKind == Ms2kPatchTree::User) {
        auto& pm = m_proc.patches();
        if (m_tree.selIndex >= 0 && m_tree.selIndex < int(pm.userCount())) {
            const int bank = m_tree.selIndex;
            const bool room = !pm.user(size_t(bank)).bank.full();
            const juce::String name = utf8(pm.user(size_t(bank)).name);
            m.addSeparator();
            m.addItem("Add Current Patch to \"" + name + "\"", haveLive && room, false, [this, bank] { pmAddCurrent(bank); toast("Added to the bank"); });
            m.addItem("Paste from Clipboard into \"" + name + "\"", clipboardHasPatch() && room, false, [this, bank] { pmPaste(bank); toast("Pasted into the bank"); });
        }
    }
    m.showMenuAsync(juce::PopupMenu::Options());
}

// ---- KNOB-FOLLOW ----
// Off: the pots are drawn where the physical pots stand (m_proc.knobs). On: from the edit buffer (programKnobs),
// for the timbre whose TIMBRE SELECT LED is lit; a pot being turned keeps the mouse's value until 300 ms after
// the last move (the firmware takes a few ms to read it back). Turning a pot always moves the physical pot.
bool Ms2kEditor::refreshKnobs()
{
    auto* r = m_proc.runner();
    if (!m_proc.knobFollow.load() || !r) {
        const bool was = m_io.knobs != m_proc.knobs;
        m_io.knobs = m_proc.knobs;
        return was;
    }
    uint8_t prog[254];
    for (int i = 0; i < 254; ++i) prog[i] = r->getEmulator().peekExternal(MS2000::kEditBufferAddr + uint32_t(i));
    uint16_t want[4][8]; bool fol[4][8];
    MS2000::programKnobs(prog, m_lit[5][2] > 0.5f ? 1 : 0, want, fol);   // TIMBRE SELECT "2" = LS5.LD02
    const bool held = (m_mouse.btn || juce::Time::getMillisecondCounter() - m_lastMoveMs < 300);
    // KNOB-FOLLOW-2 (2026-10-08, Tamas: a pot jumps while turned or snaps back to the preset value): the pots
    // follow the PROGRAM when it comes up; a pot the user has moved since then shows where the user left it,
    // not the edit buffer read back - the firmware may hold the old value (knob catch, a parameter the page
    // does not take, its own quantisation), and the read-back pulled the pot away from the hand. A new program
    // (name or TIMBRE SELECT changes) hands all pots back to the program.
    {
        uint64_t id = (m_lit[5][2] > 0.5f) ? 1u : 0u;
        for (int i = 0; i < 12; ++i) id = id * 131u + prog[i];
        if (id != m_progId) { m_progId = id; for (auto& row : m_userSet) for (auto& u : row) u = false; }
        if (m_lastMoved >= 0 && juce::Time::getMillisecondCounter() - m_lastMoveMs < 300) m_userSet[m_lastMoved / 8][m_lastMoved % 8] = true;
    }
    bool changed = m_io.knobs != m_disp;
    for (int m = 0; m < 4; ++m) for (int x = 0; x < 8; ++x) {
        if (held && m * 8 + x == m_lastMoved) continue;
        const uint16_t v = (fol[m][x] && !m_userSet[m][x]) ? want[m][x] : m_proc.knobs[m][x];
        if (v != m_disp[m][x]) { m_disp[m][x] = v; changed = true; }
    }
    m_io.knobs = m_disp;
    return changed;
}
