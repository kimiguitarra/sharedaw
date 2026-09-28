#pragma once

#include "ui/AppContext.h"
#include "ui/ChordLane.h"
#include "ui/KeyLane.h"
#include "ui/MarkerLane.h"
#include "ui/Ruler.h"
#include "ui/TempoMeterLanes.h"
#include "ui/TrackHeader.h"

/** トラックのレーン（クリップの表示・作成・移動・長さ変更、オーディオの非破壊編集）。 */
class TrackLanes  : public juce::Component,
                    public juce::SettableTooltipClient,
                    public juce::FileDragAndDropTarget
{
public:
    explicit TrackLanes (AppContext&);

    int scrollY = 0;

    /** トラックの行の上端（スクロール前）と高さ（トラックごとに変えられる）。 */
    int rowTop (int index) const;
    int rowHeightAt (int index) const;
    int rowAt (float y) const;   // y の位置のトラックの番号（なければ -1）

    std::function<void()> onOpenClip;
    std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    int getContentHeight() const;

private:
    AppContext& ctx;

    enum class Zone { none, body, leftEdge, rightEdge, fadeIn, fadeOut };
    enum class DragMode { none, move, resizeMidi, trimMidiStart, trimStart, trimEnd, fadeIn, fadeOut, rubberBand };

    struct Hit
    {
        int trackIndex = -1;
        std::string clipId;
        bool audio = false;
        Zone zone = Zone::none;
    };

    DragMode dragMode = DragMode::none;
    std::string dragTrackId, dragClipId;
    bool dragAudio = false;
    collab::Tick dragOrigStart = 0, dragOrigLength = 0;
    collab::AudioClip dragOrigAudio;
    collab::MidiClip dragOrigMidi;
    std::map<std::string, collab::Tick> dragOrigStarts;   // まとめて動かすクリップの元の位置
    juce::Rectangle<float> band;                          // 範囲選択の枠
    int splitRow = -1;                                    // はさみ: 切る位置の縦線を出すトラック
    int ghostRow = -1;                                    // 鉛筆: クリックで作られるクリップの枠
    collab::Tick ghostStart = 0, ghostEnd = 0;
    double splitTick = -1.0;
    juce::Point<float> bandStart;
    std::set<std::string> bandBase;                       // Ctrl を押して始めたときの元の選択
    double dragDownTick = 0;
    juce::String mergeId;
    bool createdByPencil = false;

    Hit findHit (juce::Point<float>) const;
    collab::Tick snap (double tick, const juce::ModifierKeys&) const;
    void paintMidiClip (juce::Graphics&, const collab::MidiClip&, juce::Rectangle<float>, juce::Colour, bool selected) const;
    void paintAudioClip (juce::Graphics&, const collab::AudioClip&, juce::Rectangle<float>, juce::Colour, bool selected);
    void showClipMenu (const collab::Track&, const std::string& clipId, bool audio);
    void createMidiClip (const std::string& trackId, int bar, bool thenDragLength);
    void updateBandSelection();
    void showLaneMenu (const collab::Track*, collab::Tick at);
    void editClip (const juce::String& description, std::function<void (collab::Track&)> fn, const juce::String& merge = {});
};

/** タイムライン全体（ルーラー、テンポ・拍子トラック、トラックヘッダー、レーン）。 */
class TimelineView  : public juce::Component,
                      private juce::ChangeListener,
                      private juce::ScrollBar::Listener
{
public:
    explicit TimelineView (AppContext&);
    ~TimelineView() override;

    std::function<void()> onOpenClip;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    /** 上の段（拍子〜マーカー）のどれにマウスがあるか（その段を明るくして、鉛筆で何を書くのか分かるように）。 */
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** マーカー〜コードのレーンの上のホイール（レーンから親へ渡ってくる）: Ctrl でズーム、Shift で横スクロール。 */
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void setPlayheadTick (double tick);

    /** テンポ・拍子・コードのレーンにフォーカスがあれば、そこで選択中のものを削除する（削除したら true）。 */
    bool deleteLaneSelection();

    /** コードトラックにフォーカスがあるときのコピー・貼り付け（処理したら true）。 */
    bool copyChord (bool cut)                         { return chordLane.hasKeyboardFocus (false) && chordLane.copySelected (cut); }
    bool pasteChord (double tick)                     { return chordLane.hasKeyboardFocus (false) && chordLane.paste (tick); }

    /** 再生位置が見えるようにスクロールする。 */
    void followPlayhead (double tick);

    static constexpr int headerWidth = 240;

private:
    AppContext& ctx;
    Ruler ruler;
    TempoLane tempoLane;
    MeterLane meterLane;
    KeyLane keyLane;
    ChordLane chordLane;
    MarkerLane markerLane;
    juce::TextButton chordMute;   // コードトラックのミュート（M）
    juce::Component* hoveredLane = nullptr;
    std::vector<juce::Component*> topLanes() const;
    TrackLanes lanes;
    /** トラックヘッダーを並べる所。空いている所を右クリックするとトラックを追加するメニュー（Cubase と同じ）。 */
    struct HeaderArea  : public juce::Component
    {
        explicit HeaderArea (AppContext& c) : ctx (c) {}
        void mouseDown (const juce::MouseEvent&) override;
        AppContext& ctx;
    };

    HeaderArea headerHolder { ctx };
    juce::OwnedArray<TrackHeader> headers;
    juce::ScrollBar hScroll { false }, vScroll { true };
    PlayheadOverlay playhead;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void handleWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&);
    void rebuildHeaders();
    void layoutHeaders();
    void moveTrackTo (const std::string& trackId, int y);
    void updateScrollBars();
    void mouseDown (const juce::MouseEvent&) override;
    void updateChordControls();
};
