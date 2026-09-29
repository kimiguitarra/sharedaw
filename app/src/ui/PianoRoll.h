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
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    PianoRollView& owner;
    double splitX = -1.0;     // はさみで切る位置（なければ -1）
    float splitY = -1.0f;
    double ghostTick = -1.0;  // 鉛筆で置かれるノートの位置（なければ ghostPitch = -1）
    int ghostPitch = -1;

    enum class Mode { none, move, resize, rubberBand };
    Mode mode = Mode::none;

    struct Orig { collab::Tick tick, length; int pitch; };
    std::map<std::string, Orig> originals;
    std::string anchorId;
    double downTick = 0;
    int downPitch = 0;
    juce::Rectangle<float> rubberBand;
    juce::String mergeId;
    bool drawingNote = false;   // 鉛筆で置いたノートの長さをドラッグで決めている

    const collab::Note* hitNote (juce::Point<float>, bool& nearRightEdge) const;
};

/** ベロシティの表示・編集（ドラッグで描くように変更）。 */
class VelocityLane  : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    explicit VelocityLane (PianoRollView& o);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    PianoRollView& owner;
    float lastX = 0;
    juce::String mergeId;
    void applyAt (float x1, float x2, float y);
};

/** オーディオクリップを選んだときに下部パネルに出す拡大波形（グリッド線つき）。 */
class AudioClipGrid  : public juce::Component
{
public:
    explicit AudioClipGrid (PianoRollView& o) : owner (o) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    PianoRollView& owner;
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
    void followPlayhead (double tick);

    /** 選択中のノートを削除する（削除したら true）。 */
    bool deleteSelectedNotes();
    bool hasSelectedNotes() const          { return ! selectedNotes.empty(); }

    /** グリッド（ノートを置く所）の左端の x（この部品の中）。全画面のとき上の段の横位置を合わせるのに使う。 */
    int getGridLeft() const                { return keyboardWidth(); }
    void selectAllNotes();
    void quantiseSelection();
    void focusEditor();

    /** ノートを短く鳴らす（クリック・入力したときの確認用）。 */
    void previewNote (int pitch, int velocity);

    // コピー・貼り付け・複製（Ctrl+C / X / V / D）
    void copySelectedNotes (bool cut);
    void pasteNotes();
    void duplicateSelectedNotes();
    bool hasNotesInClipboard() const noexcept   { return ! noteClipboard.empty(); }
    std::vector<collab::Note> noteClipboard;

    //==============================================================================
    AppContext& ctx;
    std::set<std::string> selectedNotes;
    int noteHeight = 14;
    int scrollY = 0;
    collab::Tick lastNoteLength = collab::kPpq / 4;
    int lastVelocity = 100;

    const collab::Track* getTrack() const  { return ctx.selectedTrack(); }
    const collab::MidiClip* getClip() const { return ctx.selectedClip(); }

    /** 選択中のトラックが MIDI トラックなら、クリップがなくても鉛筆で書ける（クリップを自動で作る）。 */
    bool canCreateClip() const;

    /**
        abs（曲の先頭からの tick）にノートを置けるクリップを用意して選ぶ。
        そこにクリップがあればそれ、選択中のクリップのすぐ後ろならそのクリップを小節単位で伸ばし、
        どちらでもなければその小節に新しいクリップを作る。作れなければ nullptr。
    */
    const collab::MidiClip* ensureClipAt (collab::Tick abs);
    /** 選択中のオーディオクリップ（あればピアノロールの代わりに波形を拡大表示する）。 */
    const collab::AudioClip* getAudioClip() const;
    bool isDrumTrack() const;
    /** ドラムのキットでこのノートに割り当てた音の名前（キットにない＝鳴らないなら空）。 */
    juce::String drumPieceName (int note) const;

    // ドラムのときは、キットの音だけを行にする（EZ Drummer のように、キック・スネア・ハイハット・タム・シンバルの順）
    std::vector<int> drumRows;            // 上からの行のピッチ（ドラム以外は空）
    int rowOfPitch (int pitch) const
    {
        if (drumRows.empty())
            return 127 - pitch;

        auto it = std::find (drumRows.begin(), drumRows.end(), pitch);
        return it == drumRows.end() ? -1000 : (int) (it - drumRows.begin());
    }
    int numRows() const                    { return drumRows.empty() ? 128 : (int) drumRows.size(); }
    float pitchToY (int pitch) const       { return (float) (rowOfPitch (pitch) * noteHeight - scrollY); }
    int yToPitch (float y) const
    {
        const int row = juce::jlimit (0, numRows() - 1, (int) std::floor ((y + (float) scrollY) / (float) noteHeight));
        return drumRows.empty() ? 127 - row : drumRows[(size_t) row];
    }

    /** ドラムの行を作り直す（キットが変わったとき・ノートがキットにない音にあるとき）。 */
    void rebuildDrumRows();
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
    AudioClipGrid audioGrid { *this };
    PlayheadOverlay playhead;
    juce::ScrollBar hScroll { false }, vScroll { true };

    juce::Label titleLabel;
    juce::ComboBox gridBox;
    juce::ToggleButton snapToggle { "スナップ"_ju };
    juce::TextButton quantiseButton { "クオンタイズ"_ju };

    std::string shownClipId;
    bool shownAsDrums = false;
    bool shownAsAudio = false;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void clipChanged();
    void updateTitle();
    int keyboardWidth() const              { return getAudioClip() != nullptr ? 0 : (isDrumTrack() ? 220 : 70); }
};
