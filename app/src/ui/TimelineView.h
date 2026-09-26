#pragma once

#include "ui/AppContext.h"
#include "ui/Ruler.h"
#include "ui/TempoMeterLanes.h"
#include "ui/TrackHeader.h"

/** トラックのレーン（クリップの表示・作成・移動・長さ変更）。 */
class TrackLanes  : public juce::Component,
                    public juce::SettableTooltipClient
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

    int getContentHeight() const;

private:
    AppContext& ctx;

    enum class DragMode { none, move, resize };

    struct Hit
    {
        int trackIndex = -1;
        std::string clipId;
        bool nearRightEdge = false;
    };

    DragMode dragMode = DragMode::none;
    std::string dragTrackId, dragClipId;
    collab::Tick dragOrigStart = 0, dragOrigLength = 0;
    double dragDownTick = 0;
    juce::String mergeId;

    Hit findHit (juce::Point<float>) const;
    int rowAt (float y) const;
    collab::Tick snap (double tick, const juce::ModifierKeys&) const;
    void paintMidiClip (juce::Graphics&, const collab::MidiClip&, juce::Rectangle<float>, juce::Colour, bool selected) const;
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

    /** 再生位置が見えるようにスクロールする。 */
    void followPlayhead (double tick);

    static constexpr int headerWidth = 240;

private:
    AppContext& ctx;
    Ruler ruler;
    TempoLane tempoLane;
    MeterLane meterLane;
    TrackLanes lanes;
    juce::Component headerHolder;
    juce::OwnedArray<TrackHeader> headers;
    juce::TextButton addTrackButton { "+ トラックを追加"_ju };
    juce::ScrollBar hScroll { false }, vScroll { true };
    PlayheadOverlay playhead;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void handleWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&);
    void rebuildHeaders();
    void layoutHeaders();
    void updateScrollBars();
    void showAddTrackMenu();
};
