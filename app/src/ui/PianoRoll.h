#pragma once

#include <set>

#include "ui/AppContext.h"
#include "ui/Ruler.h"
#include "ui/StaffView.h"
#include "ui/Theme.h"

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

/** ベロシティの表示・編集（選んだノートを上下にドラッグ）。 */
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
    juce::String mergeId;

    // 選んだノートを、ドラッグした分だけ上下させる（それぞれの差は保つ）
    float downY = 0;
    std::map<std::string, int> originalVelocities;
    const collab::Note* noteAt (float x) const;
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

    /** ルーラーの下に置く段（ピアノロールの画面のキー・コード・マーカー）。左の鍵盤の幅に名前、右はグリッドと同じ横位置。 */
    struct TopStrip  : public juce::Component
    {
        virtual void setLeftWidth (int) = 0;
        virtual int preferredHeight() const = 0;
    };

    /** strip を出す（nullptr で消す）。strip はこの部品の子になるが、持ち主は呼んだ側。 */
    void setTopStrip (TopStrip*);
    void selectAllNotes();
    void quantiseSelection();
    /** 五線譜で表示する（♪ ボタンと同じ）。 */
    void setStaffMode (bool on)            { staffButton.setToggleState (on, juce::sendNotificationSync); }
    void setStaffBassClef (bool on)        { bassClefButton.setToggleState (on, juce::sendNotificationSync); }

    /** 選択中のノートを nudgeTicks だけ左（-1）・右（+1）にずらす（グルーヴ用。グリッドには合わせない）。 */
    void nudgeSelection (int direction);
    /** 選択中のノートを、いちばん早いノートを起点に factor 倍に伸び縮みさせる（位置と長さ）。 */
    void stretchSelection (double factor);
    /** 選択中のノートのベロシティを delta だけ変える。 */
    void changeSelectedVelocity (int delta);
    static constexpr collab::Tick nudgeTicks = 5;   // 1 回でずらす量（1 拍 = 960 tick。大きく動かすときは何回か押す）
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
    /** 音名（その位置のキーがフラット系なら Bb、シャープ系なら F# のように。キー未設定なら C# D# …）。 */
    juce::String pitchName (int pitch, collab::Tick absoluteTick) const;
    /** ドラムのキットでこのノートに割り当てた音の名前（キットにない＝鳴らないなら空）。 */
    juce::String drumPieceName (int note) const;
    /** 行の区切り線用の仲間（PianoRollDetail::drumFamily。別名のノートは鳴らすパーツの仲間）。 */
    int drumFamilyOf (int note) const;
    const collab::BuiltinInstrumentManifest* drumManifest() const;

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
    Theme::IconButton snapToggle { "snap" };
    juce::TextButton quantiseButton { "クオンタイズ"_ju };
    juce::TextButton nudgeLeftButton { juce::String::fromUTF8 ("\xe2\x97\x80") }, nudgeRightButton { juce::String::fromUTF8 ("\xe2\x96\xb6") };
    juce::TextButton staffButton { juce::String::fromUTF8 ("\xe2\x99\xaa") };   // ♪: 五線譜とピアノロールの切り替え
    StaffView staff { *this };
    juce::TextButton bassClefButton { juce::String::fromUTF8 ("\xf0\x9d\x84\xa2") };   // 𝄢: ヘ音記号の段を出す・出さない
    bool staffMode = false;
    bool showingStaff() const              { return staffMode && ! shownAsDrums && ! shownAsAudio; }

    TopStrip* topStrip = nullptr;
    std::string shownClipId;
    bool shownAsDrums = false;
    bool shownAsAudio = false;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void clipChanged();
    void updateTitle();
    int keyboardWidth() const              { return getAudioClip() != nullptr ? 0 : (isDrumTrack() ? 220 : (showingStaff() ? 150 : 70)); }
};
