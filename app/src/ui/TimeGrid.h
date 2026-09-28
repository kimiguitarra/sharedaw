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

    //==============================================================================
    // 鉛筆ツールで、クリックしたら置かれる位置の目印（ゴースト）

    /** 縦の線と上の「+」（テンポ・拍子・キー・マーカー）。 */
    inline void drawPencilGhostLine (juce::Graphics& g, float x, int height)
    {
        g.setColour (Theme::selection.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (x - 1.0f, 0.0f, 2.0f, (float) height));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("+", juce::Rectangle<float> (x + 3.0f, 0.0f, 12.0f, (float) height), juce::Justification::centredLeft);
    }

    /** 点線の枠（クリップ・コード・ノート）。 */
    inline void drawPencilGhostBox (juce::Graphics& g, juce::Rectangle<float> r, bool withPlus = true)
    {
        g.setColour (Theme::selection.withAlpha (0.18f));
        g.fillRoundedRectangle (r, 3.0f);

        juce::Path outline;
        outline.addRoundedRectangle (r.reduced (0.5f), 3.0f);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (Theme::selection.withAlpha (0.9f));
        g.fillPath (dashed);

        if (withPlus && r.getWidth() > 14.0f && r.getHeight() > 10.0f)
        {
            g.setFont (juce::FontOptions (juce::jmin (14.0f, r.getHeight() - 2.0f), juce::Font::bold));
            g.drawText ("+", r.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
        }
    }

    /**
        クオンタイズ値の一覧を ComboBox に入れる（ID = Grid::presets() の番号 + 1）。
        5連符・7連符はめったに使わないので「その他連符」の中にまとめる。
    */
    inline void fillQuantiseBox (juce::ComboBox& box)
    {
        juce::PopupMenu others;
        int id = 1;

        for (auto& g : collab::Grid::presets())
        {
            if (g.tuplet == 5 || g.tuplet == 7)
                others.addItem (id++, toJuce (g.label()));
            else
                box.addItem (toJuce (g.label()), id++);
        }

        box.getRootMenu()->addSubMenu ("その他連符"_ju, others);
    }
}
