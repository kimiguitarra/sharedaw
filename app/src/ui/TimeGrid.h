#pragma once

#include "EditorState.h"
#include "Theme.h"
#include "collab/TempoMap.h"

/** 小節線・拍線・グリッド線の描画（タイムライン、ピアノロール、ルーラーで共通）。 */
namespace TimeGrid
{
    /** 表示範囲の小節を走査する。fn (bar, barStartTick, TimeSignature) */
    template <typename Fn>
    void forEachVisibleBar (const TimeAxis& axis, const collab::TempoMap& map, int width, Fn&& fn)
    {
        const auto firstTick = (collab::Tick) juce::jmax (0.0, axis.xToTick (0));
        const auto lastTick = (collab::Tick) axis.xToTick (width) + 1;

        for (int bar = map.tickToBar (firstTick);; ++bar)
        {
            const auto start = map.barToTick (bar);

            if (start > lastTick)
                break;

            fn (bar, start, map.timeSignatureAtBar (bar));
        }
    }

    /** 小節あたりのピクセルが小さいときに間引く間隔。 */
    inline int barLabelStep (const TimeAxis& axis, const collab::TempoMap& map, double minPixels)
    {
        const double barPixels = (double) map.timeSignatureAtBar (1).ticksPerBar() * axis.pixelsPerTick();
        int step = 1;

        while (barPixels * step < minPixels && step < 1024)
            step *= 2;

        return step;
    }

    inline void drawGrid (juce::Graphics& g, juce::Rectangle<int> area, const TimeAxis& axis,
                          const collab::TempoMap& map, const collab::Grid* subGrid)
    {
        const int step = barLabelStep (axis, map, 12.0);
        const float top = (float) area.getY(), bottom = (float) area.getBottom();
        const int x0 = area.getX();

        forEachVisibleBar (axis, map, area.getWidth(), [&] (int bar, collab::Tick start, collab::TimeSignature sig)
        {
            const double beatPixels = (double) sig.ticksPerBeat() * axis.pixelsPerTick();

            // クオンタイズ値の線（スナップがオフでも表示する）。細かすぎるときは 2 マスおき、4 マスおき…に間引く
            if (step == 1 && subGrid != nullptr)
            {
                const double stepPixels = subGrid->stepExact() * axis.pixelsPerTick();
                long long every = 1;

                while (stepPixels * (double) every < 5.0 && every < 64)
                    every *= 2;

                g.setColour (Theme::gridSub);

                for (long long k = every;; k += every)
                {
                    const auto t = start + subGrid->offsetOf (k);

                    if (t >= start + sig.ticksPerBar())
                        break;

                    g.drawVerticalLine (x0 + (int) axis.tickToX ((double) t), top, bottom);
                }
            }

            if (step == 1 && beatPixels >= 8.0)
            {
                g.setColour (Theme::gridBeat);

                for (int b = 1; b < sig.numerator; ++b)
                    g.drawVerticalLine (x0 + (int) axis.tickToX ((double) (start + b * sig.ticksPerBeat())), top, bottom);
            }

            if ((bar - 1) % step == 0)
            {
                g.setColour (Theme::gridBar);
                g.drawVerticalLine (x0 + (int) axis.tickToX ((double) start), top, bottom);
            }
        });
    }
}
