#pragma once

#include <set>

#include "ui/AppContext.h"
#include "ui/Ruler.h"

class PianoRollView;

/** ピアノロールの鍵盤（ドラムトラックでは GM ドラムマップのパーツ名を表示）。 */
class PianoKeyboard  : public juce::Component
{
public:
    explicit PianoKeyboard (PianoRollView& o) : owner (o) {}
    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    PianoRollView& owner;
};

/** ノートの表示・編集。 */
class NoteGrid  : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    explicit NoteGrid (PianoRollView& o);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    PianoRollView& owner;

    enum class Mode { none, move, resize, rubberBand };
    Mode mode = Mode::none;

    struct Orig { collab::Tick tick, length; int pitch; };
    std::map<std::string, Orig> originals;
    std::string anchorId;
    double downTick = 0;
    int downPitch = 0;
    juce::Rectangle<float> rubberBand;
    juce::String mergeId;

    const collab::Note* hitNote (juce::Point<float>, bool& nearRightEdge) const;
};

/** ベロシティの表示・編集（ドラッグで描くように変更）。 */
class VelocityLane  : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    explicit VelocityLane (PianoRollView& o);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    PianoRollView& owner;
    float lastX = 0;
    juce::String mergeId;
    void applyAt (float x1, float x2, float y);
};

/** ピアノロール（§3.2）。選択中の MIDI クリップを編集する。 */
class PianoRollView  : public juce::Component,
                       private juce::ChangeListener,
                       private juce::ScrollBar::Listener
{
public:
    explicit PianoRollView (AppContext&);
    ~PianoRollView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void setPlayheadTick (double tick);

    /** 選択中のノートを削除する（削除したら true）。 */
    bool deleteSelectedNotes();
    bool hasSelectedNotes() const          { return ! selectedNotes.empty(); }
    void selectAllNotes();
    void quantiseSelection();
    void focusEditor();

    //==============================================================================
    AppContext& ctx;
    std::set<std::string> selectedNotes;
    int noteHeight = 14;
    int scrollY = 0;
    collab::Tick lastNoteLength = collab::kPpq / 4;
    int lastVelocity = 100;

    const collab::Track* getTrack() const  { return ctx.selectedTrack(); }
    const collab::MidiClip* getClip() const { return ctx.selectedClip(); }
    bool isDrumTrack() const;

    float pitchToY (int pitch) const       { return (float) ((127 - pitch) * noteHeight - scrollY); }
    int yToPitch (float y) const           { return juce::jlimit (0, 127, 127 - (int) std::floor ((y + (float) scrollY) / (float) noteHeight)); }
    TimeAxis& axis()                       { return ctx.state.pianoRoll; }
    const TimeAxis& axis() const           { return ctx.state.pianoRoll; }

    /** クリップ内のノートを書き換える。 */
    void editNotes (const juce::String& description, std::function<void (collab::MidiClip&)>, const juce::String& mergeId = {});

    collab::Tick snap (double absoluteTick, bool floor, const juce::ModifierKeys&) const;
    void handleWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&, juce::Component* source);
    void updateScrollBars();

private:
    Ruler ruler;
    PianoKeyboard keyboard { *this };
    NoteGrid grid { *this };
    VelocityLane velocity { *this };
    PlayheadOverlay playhead;
    juce::ScrollBar hScroll { false }, vScroll { true };

    juce::Label titleLabel;
    juce::ComboBox gridBox;
    juce::ToggleButton snapToggle { "スナップ"_ju };
    juce::TextButton quantiseButton { "クオンタイズ"_ju };
    juce::Label hintLabel;

    std::string shownClipId;
    bool shownAsDrums = false;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void clipChanged();
    int keyboardWidth() const              { return isDrumTrack() ? 170 : 70; }
};
