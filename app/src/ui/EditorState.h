#pragma once

#include "Common.h"
#include "collab/Grid.h"

/** 横軸（tick ⇔ ピクセル）。タイムラインとピアノロールでそれぞれ持つ。 */
struct TimeAxis
{
    double pixelsPerQuarter = 40.0;
    double scrollTick = 0.0;

    double tickToX (double tick) const noexcept    { return (tick - scrollTick) * pixelsPerQuarter / collab::kPpq; }
    double xToTick (double x) const noexcept       { return x * collab::kPpq / pixelsPerQuarter + scrollTick; }
    double pixelsPerTick() const noexcept          { return pixelsPerQuarter / collab::kPpq; }

    /** マウス位置を中心に拡大・縮小する。 */
    void zoomAround (double x, double factor, double minPpq = 4.0, double maxPpq = 800.0)
    {
        const double tickAtX = xToTick (x);
        pixelsPerQuarter = juce::jlimit (minPpq, maxPpq, pixelsPerQuarter * factor);
        scrollTick = juce::jmax (0.0, tickAtX - x * collab::kPpq / pixelsPerQuarter);
    }
};

/** 画面の表示状態（選択、ズーム、グリッド、ループなど）。プロジェクト JSON には保存しない。 */
struct EditorState  : public juce::ChangeBroadcaster
{
    TimeAxis timeline;
    TimeAxis pianoRoll { 120.0, 0.0 };

    collab::Grid grid { 16, false, true };      // ピアノロールのグリッド
    collab::Grid timelineGrid { 4, false, true };

    std::string selectedTrackId;
    std::string selectedClipId;
    std::string selectedChordId;

    bool loopEnabled = false;
    collab::Tick loopStart = 0;
    collab::Tick loopEnd = collab::kPpq * 16;

    bool metronomeEnabled = false;
    float metronomeVolumeDb = -6.0f;

    double playheadTick = 0.0;

    void changed()      { sendChangeMessage(); }
};
