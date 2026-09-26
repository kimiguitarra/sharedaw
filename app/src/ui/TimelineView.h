#pragma once

#include "ui/AppContext.h"
#include "ui/ChordLane.h"
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
    static constexpr int rowHeight = 72;

    std::function<void()> onOpenClip;
    std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onWheel;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    int getContentHeight() const;

private:
    AppContext& ctx;

    enum class Zone { none, body, leftEdge, rightEdge, fadeIn, fadeOut };
    enum class DragMode { none, move, resizeMidi, trimStart, trimEnd, fadeIn, fadeOut };

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
    double dragDownTick = 0;
    juce::String mergeId;
    bool createdByPencil = false;

    Hit findHit (juce::Point<float>) const;
    int rowAt (float y) const;
    collab::Tick snap (double tick, const juce::ModifierKeys&) const;
    void paintMidiClip (juce::Graphics&, const collab::MidiClip&, juce::Rectangle<float>, juce::Colour, bool selected) const;
    void paintAudioClip (juce::Graphics&, const collab::AudioClip&, juce::Rectangle<float>, juce::Colour, bool selected);
    void showClipMenu (const collab::Track&, const std::string& clipId, bool audio);
    void createMidiClip (const std::string& trackId, int bar, bool thenDragLength);
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
    void resized() override;
    void setPlayheadTick (double tick);

    /** テンポ・拍子・コードのレーンにフォーカスがあれば、そこで選択中のものを削除する（削除したら true）。 */
    bool deleteLaneSelection();

    /** 再生位置が見えるようにスクロールする。 */
    void followPlayhead (double tick);

    static constexpr int headerWidth = 240;

private:
    AppContext& ctx;
    Ruler ruler;
    TempoLane tempoLane;
    MeterLane meterLane;
    ChordLane chordLane;
    juce::ToggleButton chordPlaybackToggle;
    juce::Slider chordVolume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::String chordVolumeMergeId;
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
    void updateScrollBars();
    void mouseDown (const juce::MouseEvent&) override;
    void updateChordControls();
};
