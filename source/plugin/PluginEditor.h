// MS2000R VST3 editor (phase 2, VST3-2): the vector front panel (gui/vector_panel.h - the same layout the
// standalone draws) on a JUCE Graphics backend, and a Settings page for what sits on the rear panel or inside.
#pragma once
#include <JuceHeader.h>
#include <string>
#include "PluginProcessor.h"
#include "core/lcd_gui.h"
#include "gui/vector_panel.h"
#include "core/knob_follow.h"

// PATCHES-1: the left side of the Patches page - the tree "User" (user banks) / "Data Sources" (library files).
// The editor fills `users` / `sources`; the tree draws them, reports clicks, and edits a bank's name in place.
class Ms2kPatchTree : public juce::Component
{
public:
    enum Kind { None, User, Source, UserHeader, SourceHeader };
    struct Item { juce::String name; int count = 0; bool missing = false; };
    static constexpr int kRowH = 24;
    std::vector<Item> users, sources;
    int userTotal = 0, sourceTotal = 0;
    Kind selKind = None; int selIndex = -1;                   // the bank shown on the right
    std::function<void(Kind, int)> onSelect;                  // a bank row was clicked
    std::function<void(Kind, int)> onContext;                 // right click on a row (a header gives UserHeader / SourceHeader)
    std::function<void()> onAddBank;                          // the "+" beside User
    std::function<void()> onAddSources;                       // the folder icon beside Data Sources
    std::function<void(int, const juce::String&)> onRenamed;  // user bank, the text typed

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void beginRename(int userIndex);
    void updateSize(int viewW, int viewH);  // as tall as its rows (at least the viewport): the viewport scrolls the rest
    void relayout() { updateSize(m_viewW, m_viewH); }
    void scrollToItem(Kind k, int i);       // brings a bank row into view
private:
    struct Row { Kind kind; int index; };
    std::vector<Row> rows() const;
    int rowOf(Kind k, int i) const;         // -1 if its section is collapsed
    juce::Rectangle<int> iconRect(int row, Kind headerKind) const;
    void endRename(bool commit);
    bool m_userOpen = true, m_srcOpen = true;
    int m_viewW = 0, m_viewH = 0;
    std::unique_ptr<juce::TextEditor> m_ed;
    int m_edIndex = -1;
};

// PATCHES-1: the right side - the programs of the selected bank as names in columns (top to bottom, then the next
// column), as many columns as there are programs; the viewport scrolls sideways. Click = send the program to the
// synth. In a user bank (editable) dragging a name reorders (an amber line shows where it lands); renaming is in the
// right-click menu. Up / Down arrows step through the programs. Right click = the editor's menu.
class Ms2kPatchGrid : public juce::Component
{
public:
    static constexpr int kRowH = 23, kColW = 122, kTop = 4;
    std::vector<juce::String> names;
    bool editable = false;
    int selected = -1;                      // the row last clicked
    int active = -1;                        // the program last sent to / taken from the synth (amber dot)
    juce::String emptyText;
    std::function<void(int)> onLoad;                          // row clicked
    std::function<void(int, const juce::String&)> onRenamed;  // row, the text typed
    std::function<void(int, int)> onMove;                     // row, gap (0..size) it was dropped into
    std::function<void(int)> onContext;                       // right click on a row, or -1 on empty space
    std::function<void(int)> onStep;                          // Up arrow (-1) / Down arrow (+1)

    Ms2kPatchGrid() { setWantsKeyboardFocus(true); }
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& k) override;
    void setNames(std::vector<juce::String> n);
    void beginRename(int row);
    void updateSize(int viewW, int viewH);  // as wide as its columns (at least the viewport), as tall as the viewport
    void scrollToRow(int row);
private:
    int rowsPerCol() const { return juce::jmax(1, (m_viewH - kTop) / kRowH); }
    int cellAt(juce::Point<int> p) const;   // the program under p, -1 if none
    juce::Rectangle<int> cellRect(int index) const;
    void endRename(bool commit);
    int m_viewW = 0, m_viewH = 0;
    int m_dragFrom = -1, m_gap = -1, m_gapCol = 0, m_gapSlot = 0, m_downX = 0, m_downY = 0;
    bool m_dragging = false, m_popup = false;
    std::unique_ptr<juce::TextEditor> m_ed;
    int m_edRow = -1;
};

class Ms2kEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Ms2kEditor(Ms2kProcessor& p);
    ~Ms2kEditor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    // the mouse as the panel sees it (read by the backends)
    struct Mouse {
        VPanel::V2 pos; bool inside = false, btn = false, press = false, dbl = false, shift = false;
        float dragY = 0.0f, wheel = 0.0f, lastY = 0.0f;
        std::string active;                 // the item the button went down on
    };

private:
    juce::ComponentBoundsConstrainer m_constrainer;   // the host checks sizes through it: aspect ratio of the panel
    void timerCallback() override;
    void runInput();                        // the panel's input pass (no drawing)
    void showTab(int t);
    juce::Rectangle<float> panelArea() const;
    void drawLcd(juce::Graphics& g, juce::Rectangle<float> r);
    void setupIo();

    Ms2kProcessor& m_proc;
    VPanel::IO m_io;
    float m_lit[8][12] = {};
    bool m_shown[8][12] = {};
    Mouse m_mouse;
    LcdGuiSnapshot m_lcd{};
    uint8_t m_cg[256][8] = {}; bool m_cgOk = false;
    juce::Typeface::Ptr m_face;
    juce::Image m_static;                   // the printed panel (VPanel layer false), at the display's pixel scale
    // LCD-DOTS-1: the dot matrix at its device size, remade only when the dots or the size change
    juce::Image m_lcdImg;
    std::vector<uint8_t> m_lcdShown;        // per dot of the 95 x 17 grid: 0 no dot (between cells), 1 unlit, 2 lit
    bool m_dirty = true;
    int m_tab = 0;
    int m_demoPhase = 0; juce::uint32 m_demoT0 = 0;
    double m_paintMs = 0.0;
    // R2 diagnostics, default OFF: MS2K_EDITORSHOT=<file.png> writes this editor's own pixels (never the
    // screen) 3 s after it opens; MS2K_EDITORTEST=latch first Shift+clicks pads 1 and 5 and EXIT through
    // the same input pass the mouse uses.
    int m_testTick = -1;
    juce::String m_shotPath, m_testMode, m_testResult;
    void testClick(float px, float py, bool shift);

    juce::TextButton m_tabPanel{ "Panel" }, m_tabSettings{ "Settings" };
    juce::ToggleButton m_mic2{ "AUDIO IN 2 = MIC  (rear panel switch SW1; off = LINE)" };
    juce::ToggleButton m_clock{ "Host tempo -> MIDI clock  (for GLOBAL: MIDI Clock = Ext; 24 clocks a quarter into MIDI IN)" };
    juce::ToggleButton m_dac20{ "DAC 20-bit  (the AK4522 hears the 20 MSBs; off = all 24 bits)" };
    juce::TextButton m_demo{ "Demo songs  (EXIT + GLOBAL)" };
    juce::TextButton m_syxLoad{ "Load .syx..." }, m_syxSave{ "Save all programs as .syx..." };
    juce::Label m_syxStatus;
    juce::Slider m_boost;                              // OUT-BOOST-1
    juce::Label m_boostLbl;
    juce::ToggleButton m_powerSw{ "POWER switch on the Master VOLUME: off at the minimum, as on the unit (off: a fade to 0 keeps it running)" };
    juce::ToggleButton m_sysexOut{ "MIDI OUT SysEx -> host  (off for hosts that stop on a plugin's SysEx output, e.g. VSTHost)" };
    juce::ToggleButton m_transport{ "Host transport -> Start / Stop  (the arpeggiator starts on a quarter note with the host)" };
    // PATCHES-1: the Patches page - one place for the user banks and the Data Sources (MS2000::PatchManager)
    juce::TextButton m_tabPatches{ "Patches" };
    juce::Viewport m_treeView;              // the tree scrolls when there are more banks than rows fit
    Ms2kPatchTree m_tree;
    juce::Viewport m_gridView;
    Ms2kPatchGrid m_grid;
    juce::Label m_pmStatus;                 // the page's status line (and the synth's answer to a program just sent)
    juce::Label m_toast;                    // a short message in the tab bar, for actions taken on the panel
    juce::uint32 m_toastUntil = 0;
    juce::String m_pmLastSyx;
    std::string m_activeBank; uint64_t m_activeEntry = 0;   // the program last sent to / taken from the synth: bank key, entry
    bool m_panelPopup = false;              // the right button went down on the panel (its menu is open)
    int m_pmPendingRow = -1; juce::uint32 m_pmPendingAt = 0;   // an arrow key moved to this row; it is sent once the keys rest
    void pmStep(int delta);
    void pmStatus(const juce::String& s) { m_pmStatus.setText(s, juce::dontSendNotification); }
    void toast(const juce::String& s);
    void pmSyncDisk();                      // re-read the banks from disk (when the page opens)
    void pmRebuildTree();
    void pmShow();                          // the selected bank's programs into the grid
    void pmSelect(Ms2kPatchTree::Kind k, int i);
    bool pmProgram(int row, std::vector<uint8_t>& data, std::string& name, std::string& key, uint64_t& entry);
    void pmLoadProgram(int row);
    void pmAddTo(int bank, const std::vector<uint8_t>& prog, const juce::String& what);
    void pmAddCurrent(int bank);
    void pmPaste(int bank);
    void pmCopyRow(int row);
    void pmCopyCurrent();
    void pmOverwrite(int row);
    void pmDeleteProgram(int row);
    void pmRenameProgram(int row, const juce::String& text);
    void pmMoveProgram(int row, int gap);
    void pmNewBank();
    void pmRenameBank(int bank, const juce::String& text);
    void pmDeleteBank(int bank);
    void pmExport(int bank, bool midi);
    void pmImport(bool asBankFile);
    void pmAddSources();
    void pmAddFolder();
    void pmReveal(const juce::File& f);
    void pmTreeMenu(Ms2kPatchTree::Kind k, int i);
    void pmGridMenu(int row);
    void pmPanelMenu();
    bool clipboardHasPatch() const;
    // KNOB-FOLLOW: the pots drawn from the edit buffer (or where the physical pots stand)
    juce::ToggleButton m_dspThr{ "DSP on a second core  (less load on the host's audio thread, more CPU in total; from the next start)" };
    juce::ToggleButton m_latency{ "Report latency to host" };
    juce::ToggleButton m_follow{ "Knobs show the program  (off: a pot stays where you leave it, as on the unit)" };
    uint16_t m_disp[4][8] = {};
    int m_lastMoved = -1; juce::uint32 m_lastMoveMs = 0;
    bool m_userSet[4][8] = {};                           // KNOB-FOLLOW-2: pots the user moved since the program came up
    uint64_t m_progId = 0;                               // KNOB-FOLLOW-2: name + timbre of the program the pots follow
    bool refreshKnobs();                    // true when a drawn pot changed
    juce::TextButton m_homeBtn{ "Choose the MS2000 folder..." };
    std::unique_ptr<juce::FileChooser> m_chooser;
    juce::Label m_help, m_status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ms2kEditor)
};
