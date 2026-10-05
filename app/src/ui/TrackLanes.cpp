// タイムラインのトラックの段（クリップの表示と編集）

#include "TimelineView.h"

#include "audio/MidiImport.h"

#include "TimeGrid.h"
#include "collab/Uuid.h"
#include "collab/ClipEditing.h"
#include "Dialogs.h"
#include "audio/AudioFiles.h"
#include "AutomationView.h"
#include "collab/Automation.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr float edgeGrab = 7.0f;   // クリップの端をつかめる幅
}

TrackLanes::TrackLanes (AppContext& c) : ctx (c)
{
    setWantsKeyboardFocus (true);
    setTooltip ({});
}

int TrackLanes::rowHeightAt (int index) const
{
    const auto& tracks = ctx.document.getProject().tracks;
    return index >= 0 && index < (int) tracks.size() ? ctx.state.trackHeight (tracks[(size_t) index].id) : EditorState::defaultTrackHeight;
}

int TrackLanes::rowTop (int index) const
{
    int y = topInset;

    for (int i = 0; i < index; ++i)
        y += rowHeightAt (i);

    return y;
}

int TrackLanes::getContentHeight() const
{
    return rowTop ((int) ctx.document.getProject().tracks.size());
}

int TrackLanes::rowAt (float y) const
{
    const int n = (int) ctx.document.getProject().tracks.size();
    int top = topInset - scrollY;

    for (int i = 0; i < n; ++i)
    {
        const int h = rowHeightAt (i);

        if (y >= (float) top && y < (float) (top + h))
            return i;

        top += h;
    }

    return -1;
}

TrackLanes::Hit TrackLanes::findHit (juce::Point<float> p) const
{
    Hit hit;
    hit.trackIndex = rowAt (p.y);

    if (hit.trackIndex < 0)
        return hit;

    const auto& track = ctx.document.getProject().tracks[(size_t) hit.trackIndex];
    const auto& axis = ctx.state.timeline;
    const auto& map = ctx.document.getTempoMap();
    const float rowTopY = (float) (rowTop (hit.trackIndex) - scrollY) + 3.0f;

    // オートメーションのレーンの上はクリップではない
    if (automationArea (hit.trackIndex).contains (p))
        return hit;

    for (auto it = track.midiClips.rbegin(); it != track.midiClips.rend(); ++it)
    {
        const float x1 = (float) axis.tickToX ((double) it->startTick);
        const float x2 = (float) axis.tickToX ((double) it->endTick());

        if (p.x >= x1 && p.x <= x2)
        {
            hit.clipId = it->id;
            hit.zone = x2 - p.x <= edgeGrab && x2 - x1 > edgeGrab * 2 ? Zone::rightEdge
                     : p.x - x1 <= edgeGrab && x2 - x1 > edgeGrab * 3 ? Zone::leftEdge
                                                                     : Zone::body;
            return hit;
        }
    }

    // くっついた 2 つのオーディオクリップのつなぎ目（上の縁のフェードのつまみの所は除く）
    if (p.y - rowTopY >= 10.0f)
        for (auto& right : track.audioClips)
        {
            const double joint = map.tickToSeconds ((double) right.startTick);
            const float jx = (float) axis.tickToX ((double) right.startTick);

            if (std::abs (p.x - jx) > 5.0f)
                continue;

            for (auto& left : track.audioClips)
                if (&left != &right
                     && std::abs (map.tickToSeconds ((double) left.startTick) + (double) left.lengthSamples / (double) collab::kSampleRate - joint)
                          <= collab::joinToleranceSeconds + 0.0006)
                {
                    hit.clipId = right.id;
                    hit.leftClipId = left.id;
                    hit.audio = true;
                    hit.zone = Zone::joint;
                    return hit;
                }
        }

    for (auto it = track.audioClips.rbegin(); it != track.audioClips.rend(); ++it)
    {
        const float x1 = (float) axis.tickToX ((double) it->startTick);
        const float x2 = (float) axis.tickToX ((double) collab::audioClipEndTick (*it, map));

        if (p.x < x1 || p.x > x2)
            continue;

        hit.clipId = it->id;
        hit.audio = true;

        // フェードのつまみ（paintAudioClip と同じ位置）
        const float width = x2 - x1;
        const float lengthSamples = (float) juce::jmax<collab::SampleCount> (1, it->lengthSamples);
        const float fadeInHandle = x1 + juce::jmax (4.0f, width * (float) it->fadeInSamples / lengthSamples);
        const float fadeOutHandle = x2 - juce::jmax (4.0f, width * (float) it->fadeOutSamples / lengthSamples);
        const bool nearTop = p.y - rowTopY < 10.0f;

        if (nearTop && std::abs (p.x - fadeInHandle) <= 6.0f)
            hit.zone = Zone::fadeIn;
        else if (nearTop && std::abs (p.x - fadeOutHandle) <= 6.0f)
            hit.zone = Zone::fadeOut;
        else if (nearTop && std::abs (p.x - (x1 + x2) * 0.5f) <= 7.0f)
            hit.zone = Zone::gain;   // 上の真ん中のつまみ: 上下にドラッグでクリップの音量
        else if (p.x - x1 <= edgeGrab && x2 - x1 > edgeGrab * 3)
            hit.zone = Zone::leftEdge;
        else if (x2 - p.x <= edgeGrab && x2 - x1 > edgeGrab * 3)
            hit.zone = Zone::rightEdge;
        else
            hit.zone = Zone::body;

        return hit;
    }

    return hit;
}

collab::Tick TrackLanes::snap (double tick, const juce::ModifierKeys& mods) const
{
    const auto t = (collab::Tick) std::llround (juce::jmax (0.0, tick));
    return mods.isAltDown() ? t : ctx.state.grid.snap (t, ctx.document.getTempoMap());
}

std::optional<collab::Tick> TrackLanes::magnet (const std::string& trackId, const std::string& clipId, double startTick, double seconds,
                                                int trimEdge, const juce::ModifierKeys& mods) const
{
    const auto* track = ctx.document.getProject().findTrack (trackId);

    if (track == nullptr || mods.isAltDown())
        return std::nullopt;

    const auto& map = ctx.document.getTempoMap();
    const auto& axis = ctx.state.timeline;
    const double startX = axis.tickToX (startTick);
    const double endX = axis.tickToX (map.secondsToTick (map.tickToSeconds (startTick) + seconds));
    double best = 10.0;   // ピクセル
    std::optional<collab::Tick> result;

    for (auto& o : track->audioClips)
    {
        if (o.id == clipId)
            continue;

        const auto oStart = o.startTick, oEnd = collab::audioClipEndTick (o, map);

        // 自分の頭を相手の終わりへ
        if (trimEdge <= 0)
            if (const double d = std::abs (startX - axis.tickToX ((double) oEnd)); d < best)
            {
                best = d;
                result = oEnd;
            }

        // 自分の終わりを相手の頭へ（移動なら、終わりがそこに来る開始位置）
        if (trimEdge >= 0)
            if (const double d = std::abs ((trimEdge > 0 ? startX : endX) - axis.tickToX ((double) oStart)); d < best)
            {
                best = d;
                result = trimEdge > 0 ? oStart
                                      : (collab::Tick) std::llround (map.secondsToTick (map.tickToSeconds ((double) oStart) - seconds));
            }
    }

    return result;
}

void TrackLanes::paint (juce::Graphics& g)
{
    const auto& project = ctx.document.getProject();
    const auto& map = ctx.document.getTempoMap();
    const auto& axis = ctx.state.timeline;

    g.fillAll (Theme::background);

    for (size_t i = 0; i < project.tracks.size(); ++i)
    {
        const auto& t = project.tracks[i];
        const auto row = juce::Rectangle<int> (0, rowTop ((int) i) - scrollY, getWidth(), rowHeightAt ((int) i));

        if (row.getBottom() < 0 || row.getY() > getHeight())
            continue;

        const auto laneColour = t.id == ctx.state.selectedTrackId ? Theme::lane.brighter (0.06f) : (i % 2 ? Theme::laneAlt : Theme::lane);
        g.setColour (laneColour);
        g.fillRect (row);
        TimeGrid::drawGrid (g, row, axis, map, &ctx.state.grid);
        g.setColour (Theme::background);
        g.drawHorizontalLine (row.getBottom() - 1, 0.0f, (float) getWidth());

        const auto colour = Theme::parseColour (t.color);
        const auto lane = row.withHeight (ctx.state.clipLaneHeight (t.id));   // クリップの段（下はオートメーションのレーン）

        for (auto& c : t.midiClips)
        {
            const float x1 = (float) axis.tickToX ((double) c.startTick);
            const float x2 = (float) axis.tickToX ((double) c.endTick());

            if (x2 < 0 || x1 > (float) getWidth())
                continue;

            paintMidiClip (g, c, juce::Rectangle<float> (x1, (float) lane.getY() + 3.0f, x2 - x1, (float) lane.getHeight() - 7.0f),
                           colour, ctx.state.isClipSelected (c.id));
        }

        // オーディオは半透明（後ろのグリッドが見える）。後ろ（先に並んでいる）のテイクと重なる所は、下にテイクがあることを斜線で示す
        for (size_t k = 0; k < t.audioClips.size(); ++k)
        {
            const auto& c = t.audioClips[k];
            const float x1 = (float) axis.tickToX ((double) c.startTick);
            const float x2 = (float) axis.tickToX ((double) collab::audioClipEndTick (c, map));

            if (x2 < 0 || x1 > (float) getWidth())
                continue;

            const auto r = juce::Rectangle<float> (x1, (float) lane.getY() + 3.0f, juce::jmax (2.0f, x2 - x1), (float) lane.getHeight() - 7.0f);

            // 下のテイクが透けないよう、この範囲はレーンとグリッドを描き直してから重ねる
            {
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (r.toNearestInt());
                g.setColour (laneColour);
                g.fillRect (r);
                TimeGrid::drawGrid (g, lane, axis, map, &ctx.state.grid);
            }

            paintAudioClip (g, c, r, colour, ctx.state.isClipSelected (c.id));

            for (size_t j = 0; j < k; ++j)
            {
                const auto& under = t.audioClips[j];
                const float u1 = juce::jmax (x1, (float) axis.tickToX ((double) under.startTick));
                const float u2 = juce::jmin (x2, (float) axis.tickToX ((double) collab::audioClipEndTick (under, map)));

                if (u2 - u1 < 1.0f)
                    continue;

                const auto hidden = r.withLeft (u1).withRight (u2);
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (hidden.toNearestInt());
                g.setColour (Theme::text.withAlpha (0.16f));

                for (float sx = hidden.getX() - hidden.getHeight(); sx < hidden.getRight(); sx += 7.0f)
                    g.drawLine (sx, hidden.getBottom(), sx + hidden.getHeight(), hidden.getY(), 1.0f);
            }
        }

        // 録音中: 入ってきている音をその場で描く（オーディオは入力の大きさ、MIDI は弾いたノート）
        if (auto it = ctx.engine.getLiveRecordings().find (t.id); it != ctx.engine.getLiveRecordings().end())
            paintLiveRecording (g, it->second, juce::Rectangle<float> (0.0f, (float) lane.getY() + 3.0f, (float) getWidth(), (float) lane.getHeight() - 7.0f));

        if (const auto param = ctx.state.shownAutomation (t.id); ! param.empty())
            paintAutomation (g, t, param, automationArea ((int) i));
    }

    // ループ範囲
    if (ctx.state.loopEnabled && ctx.state.loopEnd > ctx.state.loopStart)
    {
        const float x1 = (float) axis.tickToX ((double) ctx.state.loopStart);
        const float x2 = (float) axis.tickToX ((double) ctx.state.loopEnd);
        g.setColour (Theme::loopRange);
        g.fillRect (juce::Rectangle<float> (x1, 0.0f, x2 - x1, (float) getHeight()));
    }

    // 鉛筆: クリックで作られるクリップの枠
    if (ctx.state.pencil() && ghostRow >= 0 && ghostRow < (int) project.tracks.size() && dragMode == DragMode::none)
    {
        const float top = (float) (rowTop (ghostRow) - scrollY) + 3.0f;
        const float x1 = (float) axis.tickToX ((double) ghostStart), x2 = (float) axis.tickToX ((double) ghostEnd);
        TimeGrid::drawPencilGhostBox (g, { x1, top, x2 - x1, (float) rowHeightAt (ghostRow) - 7.0f });
    }

    // はさみ: 切る位置の縦線（クリップの上にいるとき）
    if (ctx.state.tool == EditTool::split && splitRow >= 0 && splitRow < (int) project.tracks.size())
    {
        const float x = (float) axis.tickToX (splitTick);
        const float top = (float) (rowTop (splitRow) - scrollY);
        g.setColour (Theme::selection);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, top, 1.5f, (float) rowHeightAt (splitRow)));
    }

    // 範囲選択の枠
    if (dragMode == DragMode::rubberBand && ! band.isEmpty())
    {
        g.setColour (Theme::selection.withAlpha (0.15f));
        g.fillRect (band);
        g.setColour (Theme::selection);
        g.drawRect (band, 1.0f);
    }

    if (project.tracks.empty())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (16.5f));
        g.drawText ("左側（トラック名の欄）の空いている所を右クリックしてトラックを追加してください（オーディオファイルはここへドラッグ＆ドロップ）"_ju,
                    getLocalBounds(), juce::Justification::centred);
    }
}

void TrackLanes::paintLiveRecording (juce::Graphics& g, const EngineBridge::LiveRecording& live, juce::Rectangle<float> row) const
{
    const auto& map = ctx.document.getTempoMap();
    const auto& axis = ctx.state.timeline;
    auto xAt = [&] (double seconds) { return (float) axis.tickToX (map.secondsToTick (seconds)); };

    double end = live.startSeconds;

    if (! live.peaks.empty())
        end = live.peaks.back().first;

    for (auto& n : live.notes)
        end = std::max (end, n.end >= 0 ? n.end : n.start);

    end = std::max (end, ctx.engine.getPositionSeconds());

    const float x1 = xAt (live.startSeconds), x2 = xAt (end);

    if (x2 < 0 || x1 > row.getRight())
        return;

    const auto area = juce::Rectangle<float> (x1, row.getY(), juce::jmax (2.0f, x2 - x1), row.getHeight());
    g.setColour (Theme::danger.withAlpha (0.18f));
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (Theme::danger.withAlpha (0.8f));
    g.drawRoundedRectangle (area, 3.0f, 1.0f);

    // オーディオ: 前の点からその点までを、その間のピークの高さで塗る
    if (! live.peaks.empty())
    {
        const float centre = area.getCentreY(), half = area.getHeight() * 0.5f - 2.0f;
        g.setColour (Theme::danger.withAlpha (0.85f));
        float prevX = x1;

        for (auto& [seconds, peak] : live.peaks)
        {
            const float x = xAt (seconds);
            const float h = half * juce::jlimit (0.0f, 1.0f, peak * ctx.state.waveformZoom);

            if (x > prevX)
                g.fillRect (juce::Rectangle<float> (prevX, centre - h, juce::jmax (1.0f, x - prevX), juce::jmax (1.0f, h * 2.0f)));

            prevX = x;
        }
    }

    // MIDI: 弾いた高さの範囲を行の高さに合わせて描く
    if (! live.notes.empty())
    {
        int lo = 127, hi = 0;

        for (auto& n : live.notes)
        {
            lo = std::min (lo, n.pitch);
            hi = std::max (hi, n.pitch);
        }

        lo -= 2;
        hi += 2;
        const float noteH = juce::jlimit (2.0f, 8.0f, area.getHeight() / (float) (hi - lo + 1));

        for (auto& n : live.notes)
        {
            const float nx1 = xAt (n.start), nx2 = xAt (n.end >= 0 ? n.end : end);
            const float y = area.getBottom() - 2.0f - (float) (n.pitch - lo + 1) / (float) (hi - lo + 1) * (area.getHeight() - 4.0f);
            g.setColour (Theme::danger.withMultipliedBrightness (0.6f + 0.4f * (float) n.velocity / 127.0f));
            g.fillRoundedRectangle (juce::Rectangle<float> (nx1, y, juce::jmax (3.0f, nx2 - nx1), noteH), 1.5f);
        }
    }

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("録音中"_ju, area.reduced (5.0f, 2.0f), juce::Justification::topLeft, false);
}

void TrackLanes::paintMidiClip (juce::Graphics& g, const collab::MidiClip& c, juce::Rectangle<float> r,
                                juce::Colour colour, bool selected) const
{
    g.setColour (colour.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (selected ? Theme::selection : colour);
    g.drawRoundedRectangle (r, 3.0f, selected ? 2.0f : 1.0f);

    if (c.notes.empty())
        return;

    int lo = 127, hi = 0;

    for (auto& n : c.notes)
    {
        lo = juce::jmin (lo, n.pitch);
        hi = juce::jmax (hi, n.pitch);
    }

    const float range = (float) juce::jmax (12, hi - lo + 1);
    const auto inner = r.reduced (2.0f, 4.0f);
    const auto& axis = ctx.state.timeline;

    g.setColour (colour.brighter (0.6f));

    for (auto& n : c.notes)
    {
        if (n.tick >= c.lengthTick || n.endTick() <= 0)
            continue;

        const float x1 = (float) axis.tickToX ((double) (c.startTick + juce::jmax<collab::Tick> (0, n.tick)));
        const float x2 = (float) axis.tickToX ((double) juce::jmin (c.startTick + n.endTick(), c.endTick()));
        const float y = inner.getBottom() - ((float) (n.pitch - lo) + 0.5f) / range * inner.getHeight();
        g.fillRect (juce::Rectangle<float> (x1, y - 1.0f, juce::jmax (1.5f, x2 - x1), 2.5f));
    }
}

void TrackLanes::paintAudioClip (juce::Graphics& g, const collab::AudioClip& c, juce::Rectangle<float> r,
                                 juce::Colour colour, bool selected)
{
    // 半透明（後ろのグリッドが見える）。重なった下のテイクは、呼ぶ側で背景を描き直して隠す（鳴るのも上だけ）
    const auto body = Theme::clipBody (colour).withMultipliedAlpha (0.55f);
    g.setColour (body);
    g.fillRoundedRectangle (r, 3.0f);

    // 上に名前の帯
    const float nameHeight = r.getHeight() >= 40.0f ? 15.0f : 0.0f;
    auto wave = r.withTrimmedTop (nameHeight).reduced (1.0f, 2.0f);

    if (nameHeight > 0.0f)
    {
        const auto bar = Theme::light ? colour.interpolatedWith (juce::Colours::white, 0.2f) : colour.withMultipliedBrightness (0.8f);
        g.setColour (bar);
        g.fillRoundedRectangle (r.withHeight (nameHeight + 3.0f), 3.0f);
        g.setColour (body);
        g.fillRect (r.withTrimmedTop (nameHeight).withHeight (3.0f));
        g.setColour (bar.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        auto label = toJuce (c.displayName);

        if (std::abs (c.gainDb) > 0.05)
            label << "  " << juce::String (c.gainDb, 1) << " dB";

        g.drawText (label, r.withHeight (nameHeight).reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    // 波形（元ファイルの offset 〜 offset + length の範囲）
    if (auto* thumb = ctx.audioCache.getThumbnail (ctx.document.getProjectDir(), c.audioHash))
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (r.toNearestInt());
        const double start = (double) c.sourceOffsetSamples / collab::kSampleRate;
        const double end = start + (double) c.lengthSamples / collab::kSampleRate;
        AudioFiles::drawWaveform (g, *thumb, wave, start, end, juce::Decibels::decibelsToGain ((float) c.gainDb) * ctx.state.waveformZoom,
                                  Theme::clipWave (colour), [this, hash = c.audioHash] (double a, double b, juce::AudioBuffer<float>& buf)
                                  {
                                      return ctx.audioCache.readSamples (ctx.document.getProjectDir(), hash, a, b, buf);
                                  });
        AudioFiles::drawTransients (g, ctx.audioCache.getTransients (ctx.document.getProjectDir(), c.audioHash), wave, start, end);
    }
    else
    {
        g.setColour (Theme::warning);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("オーディオが見つかりません"_ju, r, juce::Justification::centred);
    }

    // フェード（秒 → ピクセル。クリップの幅に対する割合で描く）
    const float fadeInW = r.getWidth() * (float) c.fadeInSamples / (float) juce::jmax<collab::SampleCount> (1, c.lengthSamples);
    const float fadeOutW = r.getWidth() * (float) c.fadeOutSamples / (float) juce::jmax<collab::SampleCount> (1, c.lengthSamples);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    juce::Path fades;
    fades.startNewSubPath (r.getX(), r.getBottom());
    fades.lineTo (r.getX() + fadeInW, r.getY());
    fades.lineTo (r.getX(), r.getY());
    fades.closeSubPath();
    fades.startNewSubPath (r.getRight(), r.getBottom());
    fades.lineTo (r.getRight() - fadeOutW, r.getY());
    fades.lineTo (r.getRight(), r.getY());
    fades.closeSubPath();
    g.fillPath (fades);

    g.setColour (Theme::text.withAlpha (0.8f));
    g.fillRect (juce::Rectangle<float> (r.getX() + juce::jmax (4.0f, fadeInW) - 3.0f, r.getY(), 6.0f, 6.0f));
    g.fillRect (juce::Rectangle<float> (r.getRight() - juce::jmax (4.0f, fadeOutW) - 3.0f, r.getY(), 6.0f, 6.0f));

    // 音量のつまみ（上の真ん中。上下にドラッグ）
    if (r.getWidth() >= 30.0f)
    {
        const auto knob = juce::Rectangle<float> (r.getCentreX() - 4.0f, r.getY() + 1.0f, 8.0f, 6.0f);
        g.setColour (std::abs (c.gainDb) > 0.05 ? Theme::selection : Theme::text.withAlpha (0.8f));
        g.fillRoundedRectangle (knob, 1.5f);
    }

    g.setColour (selected ? Theme::selection : colour.darker (0.3f));
    g.drawRoundedRectangle (r, 3.0f, selected ? 2.0f : 1.0f);
}

void TrackLanes::mouseMove (const juce::MouseEvent& e)
{
    auto hit = findHit (e.position);

    // 鉛筆: 空いている MIDI トラックの上なら、クリックで作られるクリップ（1 小節）の枠を出す
    {
        int row = -1;
        collab::Tick start = 0, end = 0;
        const auto& tracks = ctx.document.getProject().tracks;

        if (ctx.state.pencil() && hit.clipId.empty() && hit.trackIndex >= 0
            && tracks[(size_t) hit.trackIndex].type == collab::TrackType::midi)
        {
            const auto& map = ctx.document.getTempoMap();
            const int bar = map.tickToBar ((collab::Tick) juce::jmax (0.0, ctx.state.timeline.xToTick (e.position.x)));
            row = hit.trackIndex;
            start = map.barToTick (bar);
            end = map.barToTick (bar + 1);
        }

        if (row != ghostRow || start != ghostStart)
        {
            ghostRow = row;
            ghostStart = start;
            ghostEnd = end;
            repaint();
        }
    }

    // はさみはツールのカーソルと、切る位置の縦線
    if (ctx.state.tool == EditTool::split)
    {
        const int row = hit.clipId.empty() ? -1 : hit.trackIndex;
        const double tick = row >= 0 ? (double) snap (ctx.state.timeline.xToTick (e.position.x), e.mods) : -1.0;

        if (row != splitRow || std::abs (tick - splitTick) > 0.5)
        {
            splitRow = row;
            splitTick = tick;
            repaint();
        }
    }
    else if (splitRow >= 0)
    {
        splitRow = -1;
        repaint();
    }

    // オートメーションのレーン: 点の上は移動のカーソル、ほかは追加（鉛筆なら描く）
    {
        const int row = rowAt (e.position.y);
        const bool inLane = row >= 0 && automationArea (row).contains (e.position);

        if (inLane || automationHoverTrack >= 0)
        {
            automationHoverTrack = inLane ? row : -1;
            automationHover = e.position;
            repaint();
        }

        if (inLane)
        {
            const auto& t = ctx.document.getProject().tracks[(size_t) row];
            const auto param = ctx.state.shownAutomation (t.id);
            const bool onPoint = automationPointAt (t, param, e.position, automationArea (row)) >= 0;
            return setMouseCursor (onPoint ? juce::MouseCursor::DraggingHandCursor
                                           : ctx.state.pencil() ? Theme::pencilCursor() : juce::MouseCursor::CrosshairCursor);   // どちらも点を置く
        }
    }

    if (ctx.state.tool != EditTool::select && ctx.state.tool != EditTool::pencil)
        return setMouseCursor (Theme::toolCursor (ctx.state.tool));

    switch (hit.zone)
    {
        case Zone::leftEdge:
        case Zone::rightEdge:  setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case Zone::fadeIn:     setMouseCursor (Theme::fadeCursor (true)); break;
        case Zone::fadeOut:    setMouseCursor (Theme::fadeCursor (false)); break;
        case Zone::joint:      setMouseCursor (Theme::jointCursor()); break;
        case Zone::gain:       setMouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
        case Zone::none:       setMouseCursor (ctx.state.pencil() ? Theme::pencilCursor() : juce::MouseCursor::NormalCursor); break;
        case Zone::body:       setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void TrackLanes::mouseExit (const juce::MouseEvent&)
{
    if (automationHoverTrack >= 0)
    {
        automationHoverTrack = -1;
        repaint();
    }

    if (splitRow >= 0 || ghostRow >= 0)
    {
        splitRow = -1;
        ghostRow = -1;
        repaint();
    }
}

void TrackLanes::editClip (const juce::String& description, std::function<void (collab::Track&)> fn, const juce::String& merge)
{
    const auto trackId = dragTrackId.empty() ? ctx.state.selectedTrackId : dragTrackId;
    ctx.document.perform (description, [trackId, fn] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            fn (*t);
    }, merge);
}

void TrackLanes::showClipMenu (const collab::Track& track, const std::string& clipId, bool audio)
{
    juce::PopupMenu m;
    const auto trackId = track.id;

    if (! audio)
        m.addItem ("ピアノロールで開く"_ju, [this] { if (onOpenClip) onOpenClip(); });

    m.addItem ("再生位置で分割"_ju, [this] { ctx.splitAtPlayhead(); });

    if (audio)
    {
        // テイクの一覧（Pro Tools のプレイリストのように）: このクリップと重なっているテイクを並べ、選んだものを一番上にする
        {
            const auto& map = ctx.document.getTempoMap();
            const collab::AudioClip* self = nullptr;

            for (auto& c : track.audioClips)
                if (c.id == clipId)
                    self = &c;

            if (self != nullptr)
            {
                const auto start = self->startTick, end = collab::audioClipEndTick (*self, map);
                juce::PopupMenu takes;
                int count = 0;

                for (size_t i = 0; i < track.audioClips.size(); ++i)
                {
                    auto& c = track.audioClips[i];

                    if (c.startTick >= end || collab::audioClipEndTick (c, map) <= start)
                        continue;

                    ++count;
                    const bool top = i + 1 == track.audioClips.size()
                                     || std::none_of (track.audioClips.begin() + (long) i + 1, track.audioClips.end(), [&] (auto& other)
                                        { return other.startTick < collab::audioClipEndTick (c, map) && collab::audioClipEndTick (other, map) > c.startTick; });
                    const auto label = juce::String (count) + ". " + toJuce (c.displayName) + "  (" + juce::String (map.tickToBar (c.startTick)) + "小節〜)"_ju;

                    takes.addItem (label, true, top, [this, trackId, id = c.id]
                    {
                        ctx.document.perform ("テイクを上に出す"_ju, [trackId, id] (collab::Project& p)
                        {
                            if (auto* t = p.findTrack (trackId))
                            {
                                auto it = std::find_if (t->audioClips.begin(), t->audioClips.end(), [&] (auto& c) { return c.id == id; });

                                if (it != t->audioClips.end())
                                    std::rotate (it, it + 1, t->audioClips.end());   // 最後 = 一番上（鳴る）
                            }
                        });
                    });
                }

                if (count > 1)
                {
                    m.addSubMenu ("テイク（選んだものを一番上に）"_ju, takes);
                    m.addSeparator();
                }
            }
        }

        m.addItem ("ノーマライズ（-1 dB）"_ju, [this, trackId, clipId] { normaliseClip (trackId, clipId); });
        m.addItem ("クリップの音量…"_ju, [this, trackId, clipId]
        {
            double current = 0;
            if (auto* t = ctx.document.getProject().findTrack (trackId))
                for (auto& c : t->audioClips)
                    if (c.id == clipId)
                        current = c.gainDb;

            Dialogs::askText ("クリップの音量"_ju, "dB（例: -3）"_ju, juce::String (current, 1), [this, trackId, clipId] (const juce::String& text)
            {
                const double db = juce::jlimit (-60.0, 24.0, text.getDoubleValue());
                ctx.document.perform ("クリップの音量"_ju, [trackId, clipId, db] (collab::Project& p)
                {
                    if (auto* t = p.findTrack (trackId))
                        for (auto& c : t->audioClips)
                            if (c.id == clipId)
                                c.gainDb = db;
                });
            });
        });
    }
    else
    {
        // 伸び縮み: このクリップ（選んでいる MIDI クリップがあれば、それも一緒に）
        {
            auto ids = ctx.state.clipSelection();
            ids.insert (clipId);
            std::set<std::string> midiIds;

            for (auto& id : ids)
                for (auto& t : ctx.document.getProject().tracks)
                    if (t.findMidiClip (id) != nullptr)
                        midiIds.insert (id);

            m.addSubMenu ("伸び縮み（音価を変える）"_ju, AppContext::stretchMenu ([this, midiIds] (double f) { ctx.stretchMidiClips (midiIds, f); }));
        }

        m.addItem ("複製"_ju, [this, trackId, clipId]
        {
            auto newId = collab::generateUuid();
            ctx.document.perform ("クリップの複製"_ju, [trackId, clipId, newId] (collab::Project& p)
            {
                if (auto* t = p.findTrack (trackId))
                    if (auto* c = t->findMidiClip (clipId))
                    {
                        auto copy = *c;
                        copy.id = newId;
                        copy.startTick = c->endTick();

                        for (auto& n : copy.notes)
                            n.id = collab::generateUuid();

                        t->midiClips.push_back (copy);
                    }
            });
            ctx.state.selectClip (newId);
            ctx.state.changed();
        });
    }

    m.addItem ("削除"_ju, [this, trackId, clipId]
    {
        ctx.document.perform ("クリップの削除"_ju, [trackId, clipId] (collab::Project& p)
        {
            if (auto* t = p.findTrack (trackId))
            {
                std::erase_if (t->midiClips, [&] (auto& c) { return c.id == clipId; });
                std::erase_if (t->audioClips, [&] (auto& c) { return c.id == clipId; });
            }
        });
    });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void TrackLanes::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    automationDrag.reset();

    if (const int row = rowAt (e.position.y); row >= 0 && automationArea (row).contains (e.position))
        return automationMouseDown (e, row);

    auto hit = findHit (e.position);
    const auto& project = ctx.document.getProject();
    const auto tool = ctx.state.tool;
    const auto tick = snap (ctx.state.timeline.xToTick (e.position.x), e.mods);
    const bool additive = e.mods.isCommandDown() || e.mods.isShiftDown();
    dragMode = DragMode::none;
    dragTrackId = {};

    if (hit.trackIndex >= 0)
    {
        ctx.state.selectedTrackId = project.tracks[(size_t) hit.trackIndex].id;
        ctx.state.changed();
    }

    // 空いている所
    if (hit.clipId.empty())
    {
        const auto* track = hit.trackIndex >= 0 ? &project.tracks[(size_t) hit.trackIndex] : nullptr;

        if (e.mods.isPopupMenu())
            return showLaneMenu (track, tick);

        if (tool == EditTool::pencil && track != nullptr && track->type == collab::TrackType::midi)
        {
            // 鉛筆ツール: クリックした小節から 1 小節のクリップを作り、そのままドラッグで長さを決める
            const auto bar = ctx.document.getTempoMap().tickToBar ((collab::Tick) juce::jmax (0.0, ctx.state.timeline.xToTick (e.position.x)));
            createMidiClip (track->id, bar, true);
            return;
        }

        // 選択ツール: ドラッグで範囲選択（Ctrl / Shift で追加）
        if (! additive)
            ctx.state.selectClip ({});

        ctx.state.changed();

        if (tool == EditTool::select)
        {
            dragMode = DragMode::rubberBand;
            bandStart = e.position;
            band = {};
            bandBase = additive ? ctx.state.clipSelection() : std::set<std::string>();
        }

        return;
    }

    const auto& track = project.tracks[(size_t) hit.trackIndex];

    // はさみ（Cubase のツール 3）
    if (! e.mods.isPopupMenu() && tool == EditTool::split)
        return ctx.splitClipAt (hit.clipId, tick);

    // 選択: Ctrl / Shift クリックで追加・解除。選択済みのクリップを押したときは選択をそのままにする（まとめて動かす）
    if (additive && ! e.mods.isPopupMenu())
    {
        ctx.state.toggleClip (hit.clipId);
        ctx.state.changed();
        return;
    }

    if (! ctx.state.isClipSelected (hit.clipId))
        ctx.state.selectClip (hit.clipId);
    else
        ctx.state.selectedClipId = hit.clipId;

    ctx.state.changed();

    if (e.mods.isPopupMenu())
        return showClipMenu (track, hit.clipId, hit.audio);

    dragTrackId = track.id;
    dragClipId = hit.clipId;
    dragAudio = hit.audio;
    dragDownTick = ctx.state.timeline.xToTick (e.position.x);
    mergeId = juce::Uuid().toString();

    dragOrigStarts.clear();

    for (auto& id : ctx.state.clipSelection())
        dragOrigStarts[id] = ctx.clipRange (id).first;

    if (hit.audio)
    {
        for (auto& c : track.audioClips)
            if (c.id == hit.clipId)
                dragOrigAudio = c;

        dragOrigStart = dragOrigAudio.startTick;

        switch (hit.zone)
        {
            case Zone::leftEdge:  dragMode = DragMode::trimStart; break;
            case Zone::rightEdge: dragMode = DragMode::trimEnd; break;
            case Zone::fadeIn:    dragMode = DragMode::fadeIn; break;
            case Zone::fadeOut:   dragMode = DragMode::fadeOut; break;
            case Zone::gain:      dragMode = DragMode::gain; break;
            case Zone::joint:
                dragMode = DragMode::joint;

                for (auto& c : track.audioClips)
                    if (c.id == hit.leftClipId)
                        dragOrigLeft = c;

                break;
            case Zone::none:
            case Zone::body:      dragMode = DragMode::move; break;
        }
    }
    else if (auto* clip = track.findMidiClip (hit.clipId))
    {
        dragMode = hit.zone == Zone::rightEdge ? DragMode::resizeMidi
                 : hit.zone == Zone::leftEdge  ? DragMode::trimMidiStart
                                               : DragMode::move;
        dragOrigStart = clip->startTick;
        dragOrigLength = clip->lengthTick;
        dragOrigMidi = *clip;
    }
}

void TrackLanes::updateBandSelection()
{
    const auto& project = ctx.document.getProject();
    const auto& axis = ctx.state.timeline;
    const auto& map = ctx.document.getTempoMap();
    auto selection = bandBase;

    for (size_t i = 0; i < project.tracks.size(); ++i)
    {
        const float top = (float) (rowTop ((int) i) - scrollY), bottom = top + (float) rowHeightAt ((int) i);

        if (bottom < band.getY() || top > band.getBottom())
            continue;

        auto consider = [&] (const std::string& id, collab::Tick s, collab::Tick e)
        {
            const float x1 = (float) axis.tickToX ((double) s), x2 = (float) axis.tickToX ((double) e);

            if (x2 >= band.getX() && x1 <= band.getRight())
                selection.insert (id);
        };

        for (auto& c : project.tracks[i].midiClips)  consider (c.id, c.startTick, c.endTick());
        for (auto& c : project.tracks[i].audioClips) consider (c.id, c.startTick, collab::audioClipEndTick (c, map));
    }

    ctx.state.selectedClipIds = selection;

    if (! selection.count (ctx.state.selectedClipId))
        ctx.state.selectedClipId = selection.empty() ? std::string() : *selection.begin();

    ctx.state.changed();
}

void TrackLanes::showLaneMenu (const collab::Track* track, collab::Tick at)
{
    // Cubase と同じく、空いている所の右クリックでツールと貼り付けなど
    juce::PopupMenu m;
    const std::pair<EditTool, juce::String> list[] = {
        { EditTool::select, "選択（1）"_ju }, { EditTool::pencil, "鉛筆（2）"_ju }, { EditTool::split, "はさみ（3）"_ju }
    };

    for (auto& [tool, name] : list)
        m.addItem (name, true, ctx.state.tool == tool, [this, tool = tool] { ctx.state.tool = tool; ctx.state.changed(); });

    m.addSeparator();
    m.addItem ("貼り付け（再生位置へ）"_ju, ctx.hasClipsInClipboard(), false, [this]
    {
        ctx.pasteClips ((collab::Tick) std::llround (ctx.state.snapCursor (ctx.state.playheadTick, ctx.document.getTempoMap(), {})));
    });

    if (track != nullptr && track->type == collab::TrackType::midi)
    {
        const auto bar = ctx.document.getTempoMap().tickToBar (at);
        m.addItem ("ここに MIDI クリップを作成"_ju, [this, trackId = track->id, bar] { createMidiClip (trackId, bar, false); });
    }

    if (ctx.addTrackMenu)
    {
        m.addSeparator();
        m.addSubMenu ("トラックを追加"_ju, ctx.addTrackMenu());
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void TrackLanes::mouseDrag (const juce::MouseEvent& e)
{
    if (automationDrag)
        return automationMouseDrag (e);

    if (dragMode == DragMode::rubberBand)
    {
        band = juce::Rectangle<float> (bandStart, e.position);
        updateBandSelection();
        repaint();
        return;
    }

    if (dragMode == DragMode::none || (e.getDistanceFromDragStart() < 3 && ! createdByPencil))
        return;

    const auto& map = ctx.document.getTempoMap();
    const double tickNow = ctx.state.timeline.xToTick (e.position.x);
    const double delta = tickNow - dragDownTick;
    const auto clipId = dragClipId;

    if (dragMode == DragMode::resizeMidi)
    {
        const auto minLen = juce::jmax<collab::Tick> (1, ctx.state.grid.stepTicks());
        auto end = juce::jmax (dragOrigStart + minLen, snap ((double) (dragOrigStart + dragOrigLength) + delta, e.mods));

        if (createdByPencil)
        {
            // 鉛筆で作ったクリップは小節単位で伸ばす（最低 1 小節）
            const int endBar = juce::jmax (map.tickToBar (dragOrigStart) + 1, map.tickToBar ((collab::Tick) juce::jmax (0.0, tickNow)) + 1);
            end = map.barToTick (endBar);
        }

        editClip ("クリップの長さ変更"_ju, [clipId, end] (collab::Track& t)
        {
            if (auto* c = t.findMidiClip (clipId))
                c->lengthTick = end - c->startTick;
        }, mergeId);
        return;
    }

    if (dragMode == DragMode::trimMidiStart)
    {
        // 左端: 開始位置を動かす（ノートの位置は変えず、外に出たノートは隠れる）
        const auto minLen = juce::jmax<collab::Tick> (1, ctx.state.grid.stepTicks());
        const auto updated = collab::trimMidiClipStart (dragOrigMidi, snap ((double) dragOrigStart + delta, e.mods), minLen);

        editClip ("クリップの長さ変更"_ju, [updated] (collab::Track& t)
        {
            if (auto* c = t.findMidiClip (updated.id))
                *c = updated;
        }, mergeId);
        return;
    }

    if (dragAudio && dragMode != DragMode::move)
    {
        const auto orig = dragOrigAudio;
        collab::AudioClip updated = orig;

        switch (dragMode)
        {
            case DragMode::trimStart:
            {
                const double raw = juce::jmax (0.0, (double) orig.startTick + delta);
                const auto at = magnet (dragTrackId, orig.id, raw, 0.0, -1, e.mods).value_or (snap (raw, e.mods));
                updated = collab::trimAudioClipStart (orig, at, map);
                break;
            }

            case DragMode::trimEnd:
            {
                const auto sourceLength = ctx.audioCache.getLengthSamples (ctx.document.getProjectDir(), orig.audioHash);
                const double raw = (double) collab::audioClipEndTick (orig, map) + delta;
                const auto at = magnet (dragTrackId, orig.id, raw, 0.0, 1, e.mods).value_or (snap (raw, e.mods));
                updated = collab::trimAudioClipEnd (orig, at, sourceLength, map);
                break;
            }

            case DragMode::fadeIn:
            {
                const auto s = collab::samplesBetween (orig.startTick, (collab::Tick) std::llround (juce::jmax (0.0, tickNow)), map);
                updated.fadeInSamples = juce::jlimit<collab::SampleCount> (0, orig.lengthSamples - orig.fadeOutSamples, s);
                break;
            }

            case DragMode::fadeOut:
            {
                const auto s = collab::samplesBetween ((collab::Tick) std::llround (juce::jmax (0.0, tickNow)), collab::audioClipEndTick (orig, map), map);
                updated.fadeOutSamples = juce::jlimit<collab::SampleCount> (0, orig.lengthSamples - orig.fadeInSamples, s);
                break;
            }

            case DragMode::gain:
            {
                // 上へ 4 ピクセルで +1 dB（Shift で細かく）
                const double perPixel = e.mods.isShiftDown() ? 0.05 : 0.25;
                updated.gainDb = juce::jlimit (-60.0, 24.0, std::round ((orig.gainDb - perPixel * e.getDistanceFromDragStartY()) * 10.0) / 10.0);
                break;
            }

            case DragMode::joint:
            {
                // つなぎ目: 左のクリップの終わりと右のクリップの頭を、くっついたまま一緒に動かす
                const auto sourceLength = ctx.audioCache.getLengthSamples (ctx.document.getProjectDir(), dragOrigLeft.audioHash);
                const auto origJoint = orig.startTick;
                auto at = snap ((double) origJoint + delta, e.mods);
                auto left = collab::trimAudioClipEnd (dragOrigLeft, at, sourceLength, map);
                updated = collab::trimAudioClipStart (orig, at, map);

                // どちらかが元ファイルの端まで来たら、そこで止める（離れないように）
                const auto leftEnd = collab::audioClipEndTick (left, map);

                if (leftEnd != at || updated.startTick != at)
                {
                    at = at > origJoint ? juce::jmin (leftEnd, updated.startTick) : juce::jmax (leftEnd, updated.startTick);
                    left = collab::trimAudioClipEnd (dragOrigLeft, at, sourceLength, map);
                    updated = collab::trimAudioClipStart (orig, at, map);
                }

                editClip ("つなぎ目の移動"_ju, [updated, left] (collab::Track& t)
                {
                    for (auto& c : t.audioClips)
                        if (c.id == updated.id)
                            c = updated;
                        else if (c.id == left.id)
                            c = left;
                }, mergeId);
                return;
            }

            case DragMode::none:
            case DragMode::move:
            case DragMode::resizeMidi:
            case DragMode::trimMidiStart:
            case DragMode::rubberBand:
                break;
        }

        editClip (dragMode == DragMode::fadeIn || dragMode == DragMode::fadeOut ? "フェード"_ju
                  : dragMode == DragMode::gain ? "クリップの音量"_ju : "トリム"_ju,
                  [updated] (collab::Track& t)
        {
            for (auto& c : t.audioClips)
                if (c.id == updated.id)
                    c = updated;
        }, mergeId);
        return;
    }

    // 複数のクリップを選んでいるときは、まとめて同じだけ動かす（トラックはそのまま）
    const auto newStart = snap ((double) dragOrigStart + delta, e.mods);

    if (dragOrigStarts.size() > 1)
    {
        const auto shift = newStart - dragOrigStart;
        auto origs = dragOrigStarts;

        ctx.document.perform ("クリップの移動"_ju, [origs, shift] (collab::Project& p)
        {
            for (auto& t : p.tracks)
            {
                for (auto& c : t.midiClips)
                    if (auto it = origs.find (c.id); it != origs.end())
                        c.startTick = std::max<collab::Tick> (0, it->second + shift);

                for (auto& c : t.audioClips)
                    if (auto it = origs.find (c.id); it != origs.end())
                        c.startTick = std::max<collab::Tick> (0, it->second + shift);
            }
        }, mergeId);
        return;
    }

    // 移動（同じ種類のトラックへなら、トラックをまたいで移動できる）
    const auto& project = ctx.document.getProject();
    auto targetTrackId = dragTrackId;
    const auto wantType = dragAudio ? collab::TrackType::audio : collab::TrackType::midi;

    if (const int row = rowAt (e.position.y); row >= 0)
        if (project.tracks[(size_t) row].type == wantType)
            targetTrackId = project.tracks[(size_t) row].id;

    // オーディオは、同じトラックの別のクリップの端の近くに来たらぴったりくっつける（スナップがオフでも）
    auto moveStart = newStart;

    if (dragAudio)
        if (auto m = magnet (targetTrackId, clipId, juce::jmax (0.0, (double) dragOrigStart + delta),
                             (double) dragOrigAudio.lengthSamples / (double) collab::kSampleRate, 0, e.mods))
            moveStart = juce::jmax<collab::Tick> (0, *m);

    const auto fromId = dragTrackId;
    const bool audio = dragAudio;

    ctx.document.perform ("クリップの移動"_ju, [fromId, targetTrackId, clipId, newStart = moveStart, audio] (collab::Project& p)
    {
        auto* from = p.findTrack (fromId);
        auto* to = p.findTrack (targetTrackId);

        if (from == nullptr || to == nullptr)
            return;

        auto moveIn = [&] (auto& fromClips, auto& toClips)
        {
            auto it = std::find_if (fromClips.begin(), fromClips.end(), [&] (auto& c) { return c.id == clipId; });

            if (it == fromClips.end())
                return;

            it->startTick = newStart;

            if (from != to)
            {
                toClips.push_back (*it);
                fromClips.erase (it);
            }
        };

        if (audio)
            moveIn (from->audioClips, to->audioClips);
        else
            moveIn (from->midiClips, to->midiClips);
    }, mergeId);

    if (targetTrackId != dragTrackId)
    {
        dragTrackId = targetTrackId;
        ctx.state.selectedTrackId = targetTrackId;
        ctx.state.changed();
    }
}

void TrackLanes::mouseUp (const juce::MouseEvent&)
{
    if (automationDrag)
    {
        automationDrag.reset();
        repaint();
    }

    if (dragMode == DragMode::rubberBand)
        repaint();

    dragMode = DragMode::none;
    createdByPencil = false;
    ctx.document.endMerge();
}

void TrackLanes::mouseDoubleClick (const juce::MouseEvent& e)
{
    // MIDI クリップをダブルクリックしたらピアノロールで開く（空いている所では何もしない。作成は鉛筆ツールで）
    auto hit = findHit (e.position);

    if (hit.trackIndex >= 0 && ! hit.clipId.empty() && ! hit.audio && onOpenClip)
        onOpenClip();
}

void TrackLanes::createMidiClip (const std::string& trackId, int bar, bool thenDragLength)
{
    const auto& map = ctx.document.getTempoMap();
    collab::MidiClip clip;
    clip.id = collab::generateUuid();
    clip.startTick = map.barToTick (bar);
    clip.lengthTick = map.barToTick (bar + 1) - clip.startTick;
    mergeId = juce::Uuid().toString();

    ctx.document.perform ("クリップの作成"_ju, [trackId, clip] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            t->midiClips.push_back (clip);
    }, mergeId);

    ctx.state.selectedTrackId = trackId;
    ctx.state.selectClip (clip.id);
    ctx.state.changed();

    if (thenDragLength)
    {
        // 作成と長さの変更を 1 回の操作として元に戻せるように、同じ mergeId で長さを変える
        dragMode = DragMode::resizeMidi;
        dragTrackId = trackId;
        dragClipId = clip.id;
        dragAudio = false;
        dragOrigStart = clip.startTick;
        dragOrigLength = clip.lengthTick;
        dragDownTick = (double) (clip.startTick + clip.lengthTick);
        createdByPencil = true;
    }
    else
    {
        ctx.document.endMerge();
    }
}

void TrackLanes::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (ctx.state.waveformWheel (e, w))
        return;

    if (onWheel)
        onWheel (e, w);
}

bool TrackLanes::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension (AudioFiles::supportedWildcard().replace ("*", "")) || MidiImport::isMidiFile (juce::File (f)))
            return true;

    return false;
}

void TrackLanes::filesDropped (const juce::StringArray& paths, int x, int y)
{
    juce::Array<juce::File> files, midiFiles;

    for (auto& p : paths)
        (MidiImport::isMidiFile (juce::File (p)) ? midiFiles : files).add (juce::File (p));

    std::string trackId, midiTrackId;

    if (const int row = rowAt ((float) y); row >= 0 && row < (int) ctx.document.getProject().tracks.size())
    {
        const auto& t = ctx.document.getProject().tracks[(size_t) row];
        (t.type == collab::TrackType::audio ? trackId : midiTrackId) = t.id;
    }

    const auto& map = ctx.document.getTempoMap();
    const auto tick = map.barToTick (map.tickToBar ((collab::Tick) juce::jmax (0.0, ctx.state.timeline.xToTick ((double) x))));

    // ドロップの処理中にモーダルな進捗表示を出さないよう、少し後で読み込む
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TrackLanes> (this), files, midiFiles, trackId, midiTrackId, tick]
    {
        if (safe == nullptr)
            return;

        if (! midiFiles.isEmpty())
            safe->ctx.importMidiFiles (midiFiles, midiTrackId, tick);

        if (! files.isEmpty())
            safe->ctx.importAudioFiles (files, trackId, tick);
    });
}

void TrackLanes::normaliseClip (const std::string& trackId, const std::string& clipId)
{
    auto* t = ctx.document.getProject().findTrack (trackId);

    if (t == nullptr)
        return;

    for (auto& c : t->audioClips)
    {
        if (c.id != clipId)
            continue;

        auto* thumb = ctx.audioCache.getThumbnail (ctx.document.getProjectDir(), c.audioHash);

        if (thumb == nullptr)
            return;

        // クリップの範囲（元ファイルの offset 〜 offset + length）のピーク
        const double start = (double) c.sourceOffsetSamples / collab::kSampleRate;
        const double end = start + (double) c.lengthSamples / collab::kSampleRate;
        float peak = 0.0f;

        for (int ch = 0; ch < thumb->getNumChannels(); ++ch)
        {
            float mn = 0.0f, mx = 0.0f;
            thumb->getApproximateMinMax (start, end, ch, mn, mx);
            peak = juce::jmax (peak, std::abs (mn), std::abs (mx));
        }

        if (peak <= 1.0e-5f)
            return;

        const double db = juce::jlimit (-60.0, 24.0, std::round ((-1.0 - juce::Decibels::gainToDecibels ((double) peak)) * 10.0) / 10.0);

        ctx.document.perform ("ノーマライズ"_ju, [trackId, clipId, db] (collab::Project& p)
        {
            if (auto* track = p.findTrack (trackId))
                for (auto& clip : track->audioClips)
                    if (clip.id == clipId)
                        clip.gainDb = db;
        });
        return;
    }
}

//==============================================================================
juce::Rectangle<float> TrackLanes::automationArea (int index) const
{
    const auto& tracks = ctx.document.getProject().tracks;

    if (index < 0 || index >= (int) tracks.size())
        return {};

    const auto& id = tracks[(size_t) index].id;
    const int h = ctx.state.automationHeight (id);

    if (h <= 0)
        return {};

    const int top = rowTop (index) - scrollY + ctx.state.clipLaneHeight (id);
    return juce::Rectangle<int> (0, top, getWidth(), h - 1).toFloat();
}

float TrackLanes::automationY (const std::string& param, double value, juce::Rectangle<float> area) const
{
    const auto r = area.reduced (0.0f, 6.0f);
    return r.getBottom() - (float) AutomationView::toNorm (param, value) * r.getHeight();
}

double TrackLanes::automationValue (const std::string& param, float y, juce::Rectangle<float> area) const
{
    const auto r = area.reduced (0.0f, 6.0f);
    return AutomationView::fromNorm (param, (double) ((r.getBottom() - y) / juce::jmax (1.0f, r.getHeight())));
}

int TrackLanes::automationPointAt (const collab::Track& t, const std::string& param, juce::Point<float> p, juce::Rectangle<float> area) const
{
    const auto* lane = t.findAutomation (param);

    if (lane == nullptr)
        return -1;

    int best = -1;
    float bestDistance = 8.0f;

    for (size_t i = 0; i < lane->points.size(); ++i)
    {
        const auto& pt = lane->points[i];
        const juce::Point<float> q ((float) ctx.state.timeline.tickToX ((double) pt.tick), automationY (param, pt.value, area));

        if (const float d = q.getDistanceFrom (p); d < bestDistance)
        {
            bestDistance = d;
            best = (int) i;
        }
    }

    return best;
}

void TrackLanes::paintAutomation (juce::Graphics& g, const collab::Track& t, const std::string& param, juce::Rectangle<float> area) const
{
    const auto& axis = ctx.state.timeline;
    const auto colour = Theme::parseColour (t.color);
    const auto* lane = t.findAutomation (param);

    g.setColour (Theme::panel.withAlpha (0.55f));
    g.fillRect (area);
    g.setColour (Theme::background);
    g.drawHorizontalLine ((int) area.getY(), 0.0f, area.getRight());

    // 基準の線（音量は 0 dB、パンは C）
    g.setColour (Theme::gridBeat.withAlpha (0.6f));
    g.drawHorizontalLine ((int) automationY (param, 0.0, area), 0.0f, area.getRight());

    if (lane == nullptr || lane->points.empty())
    {
        // 点がないとき: ミキサーの値を点線で、使い方を薄く
        const float y = automationY (param, collab::mixerValue (t, param), area);
        const float dashes[] = { 5.0f, 4.0f };
        g.setColour (colour.withAlpha (0.7f));
        g.drawDashedLine ({ 0.0f, y, area.getRight(), y }, dashes, 2, 1.5f);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.5f));
        g.drawText (AutomationView::paramName (param) + juce::String::fromUTF8 ("：クリックで点を追加（グリッドに合わせる）"),   // utf8-std
                    area.reduced (8.0f, 2.0f).removeFromTop (16.0f), juce::Justification::centredLeft, true);
        return;
    }

    // 線（点の間は値で直線。音量は高さが dB に比例しないので、細かく区切って描く）
    juce::Path line;
    const float step = 3.0f;

    for (float x = area.getX(); x <= area.getRight() + step; x += step)
    {
        const auto tick = (collab::Tick) std::llround (juce::jmax (0.0, axis.xToTick (x)));
        const float y = automationY (param, collab::automationValueAt (lane->points, tick, 0.0), area);

        if (x <= area.getX())
            line.startNewSubPath (x, y);   // Path::isEmpty() は点だけでは true のままなので、最初の x で始める
        else
            line.lineTo (x, y);
    }

    g.setColour (colour.withAlpha (0.18f));
    juce::Path fill (line);
    fill.lineTo (area.getRight() + step, area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();
    g.fillPath (fill);
    g.setColour (colour.darker (0.25f));
    g.strokePath (line, juce::PathStrokeType (2.0f));

    // 点
    const int dragged = automationDrag && automationDrag->trackId == t.id && ! automationDrag->freehand ? automationDrag->pointIndex : -1;
    int labelIndex = -1;

    for (size_t i = 0; i < lane->points.size(); ++i)
    {
        const auto& p = lane->points[i];
        const float x = (float) axis.tickToX ((double) p.tick);

        if (x < -10.0f || x > area.getRight() + 10.0f)
            continue;

        const juce::Point<float> c (x, automationY (param, p.value, area));
        const bool hot = automationHoverTrack >= 0 && c.getDistanceFrom (automationHover) < 8.0f;
        g.setColour (hot ? Theme::selection : colour.brighter (0.5f));
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (c));
        g.setColour (Theme::background);
        g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (c), 1.0f);

        if (hot)
            labelIndex = (int) i;
    }

    // 動かしている点・マウスの下の点の値
    if (dragged >= 0 || labelIndex >= 0)
    {
        // 動かした後の点はどれか分からないので、マウスに一番近い点の値を出す
        const auto at = automationDrag ? automationDrag->last : automationHover;
        int nearest = -1;
        float best = 1.0e9f;

        for (size_t i = 0; i < lane->points.size(); ++i)
        {
            const juce::Point<float> c ((float) axis.tickToX ((double) lane->points[i].tick), automationY (param, lane->points[i].value, area));

            if (const float d = c.getDistanceFrom (at); d < best)
            {
                best = d;
                nearest = (int) i;
            }
        }

        if (nearest >= 0)
        {
            const auto& p = lane->points[(size_t) nearest];
            const juce::Point<float> c ((float) axis.tickToX ((double) p.tick), automationY (param, p.value, area));
            const auto text = AutomationView::valueText (param, p.value);
            auto box = juce::Rectangle<float> (70.0f, 18.0f).withPosition (c.x + 8.0f, juce::jlimit (area.getY(), area.getBottom() - 18.0f, c.y - 20.0f));
            g.setColour (Theme::panel.withAlpha (0.92f));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
            g.drawText (text, box, juce::Justification::centred);
        }
    }

    // パラメーターの名前（左上）
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (AutomationView::paramName (param), area.reduced (8.0f, 2.0f).removeFromTop (16.0f), juce::Justification::centredLeft, true);
}

void TrackLanes::setAutomation (const std::string& trackId, const std::string& param, std::vector<collab::AutomationPoint> points,
                                const juce::String& description, const juce::String& merge)
{
    ctx.editTrack (trackId, description, [param, points] (collab::Track& t) { collab::setAutomation (t, param, points); }, merge);
}

void TrackLanes::automationMouseDown (const juce::MouseEvent& e, int trackIndex)
{
    const auto& t = ctx.document.getProject().tracks[(size_t) trackIndex];
    const auto param = ctx.state.shownAutomation (t.id);
    const auto area = automationArea (trackIndex);

    if (ctx.state.selectedTrackId != t.id)
    {
        ctx.state.selectedTrackId = t.id;
        ctx.state.changed();
    }

    if (e.mods.isPopupMenu())
        return showAutomationMenu (t.id, param);

    const auto* lane = t.findAutomation (param);
    std::vector<collab::AutomationPoint> points = lane != nullptr ? lane->points : std::vector<collab::AutomationPoint>();
    const int index = automationPointAt (t, param, e.position, area);

    // 点をダブルクリックで消す
    if (e.getNumberOfClicks() > 1)
    {
        if (index >= 0)
        {
            points.erase (points.begin() + index);
            setAutomation (t.id, param, points, "オートメーションの点を削除"_ju);
        }

        return;
    }

    AutomationDrag d;
    d.trackId = t.id;
    d.param = param;
    d.last = e.position;
    d.downTick = ctx.state.timeline.xToTick (e.position.x);
    mergeId = juce::Uuid().toString();

    if (index >= 0)
    {
        d.original = points;
        d.pointIndex = index;
        automationDrag = d;
        return;
    }

    // 鉛筆・選択とも（Cubase と同じ）: 押した所（時間はグリッド）に点を置いて、そのままドラッグで動かせる
    const collab::AutomationPoint added { snap (ctx.state.timeline.xToTick (e.position.x), e.mods), automationValue (param, e.position.y, area) };
    collab::replaceAutomation (points, added.tick, added.tick, { added }, collab::automationRange (param));
    setAutomation (t.id, param, points, "オートメーションの点を追加"_ju, mergeId);

    d.original = points;
    d.pointIndex = (int) (std::find (points.begin(), points.end(), added) - points.begin());
    automationDrag = d;
    repaint();
}

void TrackLanes::automationMouseDrag (const juce::MouseEvent& e)
{
    auto& d = *automationDrag;
    const int row = ctx.document.getProject().indexOfTrack (d.trackId);
    const auto area = automationArea (row);

    if (area.isEmpty())
        return;

    const auto range = collab::automationRange (d.param);
    auto points = d.original;

    if (d.freehand)
    {
        // 前の位置から今の位置まで、10 ピクセルごとに点を置く
        const auto from = d.drawn.empty() ? e.position : d.last;
        const float distance = std::abs (e.position.x - from.x);
        const int steps = juce::jmax (1, (int) (distance / 10.0f));

        for (int k = 0; k <= steps; ++k)
        {
            const auto pt = from + (e.position - from) * ((float) k / (float) steps);
            const auto tick = (collab::Tick) std::llround (juce::jmax (0.0, ctx.state.timeline.xToTick (pt.x)));
            d.drawn[tick] = automationValue (d.param, juce::jlimit (area.getY(), area.getBottom(), pt.y), area);
        }

        d.last = e.position;
        std::vector<collab::AutomationPoint> drawn;

        for (auto& [tick, value] : d.drawn)
            drawn.push_back ({ tick, value });

        collab::replaceAutomation (points, drawn.front().tick, drawn.back().tick, drawn, range);
        setAutomation (d.trackId, d.param, points, "オートメーションを描く"_ju, mergeId);
        return;
    }

    if (d.pointIndex < 0 || d.pointIndex >= (int) points.size())
        return;

    // 点を動かす（時間はスナップ、Alt で自由に。値はマウスの高さ）
    d.last = e.position;
    const auto moved = collab::AutomationPoint { snap ((double) points[(size_t) d.pointIndex].tick + (ctx.state.timeline.xToTick (e.position.x) - d.downTick), e.mods),
                                                 automationValue (d.param, juce::jlimit (area.getY(), area.getBottom(), e.position.y), area) };
    points.erase (points.begin() + d.pointIndex);
    collab::replaceAutomation (points, moved.tick, moved.tick, { moved }, range);
    setAutomation (d.trackId, d.param, points, "オートメーションの点を移動"_ju, mergeId);
}

void TrackLanes::showAutomationMenu (const std::string& trackId, const std::string& param)
{
    juce::PopupMenu m;
    auto* t = ctx.document.getProject().findTrack (trackId);

    for (auto& p : collab::automationParams())
        m.addItem (AutomationView::paramName (p) + (t != nullptr && t->findAutomation (p) != nullptr ? " *" : ""), true, p == param, [this, trackId, p]
        {
            ctx.state.automationShown[trackId] = p;
            ctx.state.changed();
        });

    m.addSeparator();
    m.addItem ("このレーンの点を全部消す"_ju, t != nullptr && t->findAutomation (param) != nullptr, false, [this, trackId, param]
    {
        setAutomation (trackId, param, {}, "オートメーションを消す"_ju);
    });
    m.addItem ("レーンを隠す"_ju, [this, trackId]
    {
        ctx.state.automationShown.erase (trackId);
        ctx.state.changed();
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}
