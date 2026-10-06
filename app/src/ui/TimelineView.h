#pragma once

#include "ui/AppContext.h"
#include "ui/ChordLane.h"
#include "ui/KeyLane.h"
#include "ui/MarkerLane.h"
#include "ui/Ruler.h"
#include "ui/TempoMeterLanes.h"
#include "ui/TrackHeader.h"
#include "ui/PaneResizer.h"

/** トラックのレーン（クリップの表示・作成・移動・長さ変更、オーディオの非破壊編集）。 */
class TrackLanes  : public juce::Component,
                    public juce::SettableTooltipClient,
                    public juce::FileDragAndDropTarget
{
public:
    explicit TrackLanes (AppContext&);

    int scrollY = 0;
    int topInset = 0;   // 上の段（拍子〜マーカー）の高さ。トラックの行はその下から始まり、一緒にスクロールする

    /** トラックの行の上端（スクロール前）と高さ（トラックごとに変えられる）。 */
    int rowTop (int index) const;
    int rowHeightAt (int index) const;
    int rowAt (float y) const;   // y の位置のトラックの番号（なければ -1）

    std::function<void()> onOpenClip;
    std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

    /**
        1 つのトラックだけを全体の高さで出す（下の波形の画面）。空ならすべてのトラック（タイムライン）。
        axis: 横の位置（nullptr ならタイムラインの位置）。
    */
    std::string soloTrackId;
    const TimeAxis* axisOverride = nullptr;
    const TimeAxis& axis() const                    { return axisOverride != nullptr ? *axisOverride : ctx.state.timeline; }

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

    enum class Zone { none, body, leftEdge, rightEdge, fadeIn, fadeOut, gain, joint };   // joint: くっついた 2 つのクリップのつなぎ目
    enum class DragMode { none, move, resizeMidi, trimMidiStart, trimStart, trimEnd, fadeIn, fadeOut, gain, joint, rubberBand };

    struct Hit
    {
        int trackIndex = -1;
        std::string clipId;
        bool audio = false;
        Zone zone = Zone::none;
        std::string leftClipId;   // joint: つなぎ目の左のクリップ（clipId は右のクリップ）
    };

    DragMode dragMode = DragMode::none;
    std::string dragTrackId, dragClipId;
    bool dragAudio = false;
    collab::Tick dragOrigStart = 0, dragOrigLength = 0;
    collab::AudioClip dragOrigAudio, dragOrigLeft;   // dragOrigLeft: つなぎ目を動かすときの左のクリップ
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

    // オートメーションのレーン（トラックの行の下の段）。鉛筆でクリックして点を置く（置く所を薄く出す）、ドラッグで移動・ダブルクリックで削除
    struct AutomationDrag
    {
        std::string trackId, param;
        std::vector<collab::AutomationPoint> original;
        int pointIndex = -1;                      // 動かしている点（original の番号）
        bool freehand = false;                    // 鉛筆で描いている
        std::map<collab::Tick, double> drawn;
        juce::Point<float> last;
        double downTick = 0;
    };

    std::optional<AutomationDrag> automationDrag;
    int automationHoverTrack = -1;
    juce::Point<float> automationHover;

    juce::Rectangle<float> automationArea (int trackIndex) const;   // 出していなければ空
    int clipLaneHeight (const std::string& trackId) const;
    float automationY (const std::string& param, double value, juce::Rectangle<float> area) const;
    double automationValue (const std::string& param, float y, juce::Rectangle<float> area) const;
    int automationPointAt (const collab::Track&, const std::string& param, juce::Point<float>, juce::Rectangle<float> area) const;
    void paintAutomation (juce::Graphics&, const collab::Track&, const std::string& param, juce::Rectangle<float> area) const;
    void paintAutomationGhost (juce::Graphics&, const collab::Track&, const std::string& param, juce::Rectangle<float> area) const;
    void automationMouseDown (const juce::MouseEvent&, int trackIndex);
    void automationMouseDrag (const juce::MouseEvent&);
    void showAutomationMenu (const std::string& trackId, const std::string& param);
    void setAutomation (const std::string& trackId, const std::string& param, std::vector<collab::AutomationPoint>,
                        const juce::String& description, const juce::String& merge = {});
    collab::Tick snap (double tick, const juce::ModifierKeys&) const;

    /**
        オーディオクリップの端を、同じトラックの別のクリップの端へ吸い付ける（スナップがオフでも、近く（10 ピクセル以内）なら）。
        start〜start+seconds のクリップについて、吸い付いた開始位置を返す。なければ std::nullopt。Alt で吸い付けない。
        trimEdge: 0 = 移動（両端）、-1 = 左端だけ、1 = 右端だけ。
    */
    std::optional<collab::Tick> magnet (const std::string& trackId, const std::string& clipId, double startTick, double seconds,
                                        int trimEdge, const juce::ModifierKeys&) const;
    void paintMidiClip (juce::Graphics&, const collab::MidiClip&, juce::Rectangle<float>, juce::Colour, bool selected) const;
    void paintAudioClip (juce::Graphics&, const collab::AudioClip&, juce::Rectangle<float>, juce::Colour, bool selected);
    void reverseClip (const std::string& trackId, const std::string& clipId);
    void paintLiveRecording (juce::Graphics&, const EngineBridge::LiveRecording&, juce::Rectangle<float> row) const;
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

    /**
        上の段の何もない所を選択ツールでクリックしたら、そこへ再生位置を移す（ルーラーまで行かなくてよい）。
        ドラッグしたら矩形の範囲選択（かかった段のコード・マーカーを選ぶ。まとめてコピー・貼り付け・削除できる）。
    */
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** 範囲選択したコード・マーカーのコピー（cut なら消す）と、再生位置への貼り付け。処理したら true。 */
    bool copyRange (bool cut);
    bool pasteRange (double playheadTick);
    static bool hasRangeClipboard();
    bool deleteRange();

    /** マーカー〜コードのレーンの上のホイール（レーンから親へ渡ってくる）: Ctrl でズーム、Shift で横スクロール。 */
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void setPlayheadTick (double tick);
    void repaintLanes()                     { lanes.repaint(); }
    TrackLanes& getLanes() noexcept         { return lanes; }   // 動作確認（--smoke-test）でマウスの操作を送る

    /** テンポ・拍子・コードのレーンにフォーカスがあれば、そこで選択中のものを削除する（削除したら true）。 */
    bool deleteLaneSelection();

    /** コードトラックにフォーカスがあるときのコピー・貼り付け（処理したら true）。 */
    bool copyChord (bool cut)                         { return chordLane.hasKeyboardFocus (false) && chordLane.copySelected (cut); }
    bool pasteChord (double tick)                     { return chordLane.hasKeyboardFocus (false) && chordLane.paste (tick); }

    /** 再生位置が見えるようにスクロールする。 */
    void followPlayhead (double tick);

    /** トラックヘッダーの列の幅（境目をドラッグで変える。この PC の設定）。 */
    int getHeaderWidth() const noexcept             { return headerWidth; }
    void setHeaderWidth (int w);
    std::function<void()> onHeaderWidthChanged;
    static constexpr int minHeaderWidth = 130, maxHeaderWidth = 400;

    /** 上の段の並びを変えたとき（この PC の設定として保存する）。 */
    std::function<void()> onLaneOrderChanged;

    /** 上の段の名前と色（ピアノロールの画面でも使う）。 */
    static juce::String laneTitle (const std::string& key);
    static juce::Colour laneColour (const std::string& key);

private:
    AppContext& ctx;
    int headerWidth = 170;
    PaneResizer headerResizer;
    int headerWidthAtDrag = 0;
    Ruler ruler;
    TempoLane tempoLane;
    MeterLane meterLane;
    KeyLane keyLane;
    ChordLane chordLane;
    MarkerLane markerLane;
    juce::Component* hoveredLane = nullptr;
    bool bandCandidate = false, banding = false;
    juce::Rectangle<int> band;             // 範囲選択の矩形（TimelineView の座標）
    juce::Point<int> bandStart;

    /** 上の段（ctx.state.laneOrder の順）。 */
    std::vector<juce::Component*> topLanes() const;
    juce::Component* laneForKey (const std::string&) const;
    void layoutTopLanes();
    void stopFollowing();

    /** 上の段の見出し（左の列）。ドラッグで段を並べ替える。コードの段にはミュート（M）。 */
    class LaneHeaders  : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        explicit LaneHeaders (TimelineView&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        juce::TextButton chordMute;   // コードトラックのミュート（M）

    private:
        TimelineView& owner;
        int dragIndex = -1, dropIndex = -1;
        int indexAt (int y) const;
    };

    LaneHeaders laneHeaders { *this };
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
    void updateChordControls();
};
