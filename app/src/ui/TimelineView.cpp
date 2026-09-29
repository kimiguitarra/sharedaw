#include "TimelineView.h"

#include "audio/MidiImport.h"

#include "TimeGrid.h"
#include "collab/Uuid.h"
#include "collab/ClipEditing.h"
#include "Dialogs.h"
#include "audio/AudioFiles.h"

#include <algorithm>
#include <vector>

namespace
{
    constexpr int rulerHeight = 26;
    constexpr int laneHeight = 28;
    constexpr int chordLaneHeight = 40;
    constexpr int topLanesHeight = laneHeight * 4 + chordLaneHeight;   // 拍子・テンポ・キー・コード・マーカー（トラックと一緒にスクロール）
    constexpr int scrollBarSize = 12;
    constexpr float edgeGrab = 7.0f;
}

//==============================================================================
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
    return mods.isAltDown() ? t : ctx.state.timelineGrid.snap (t, ctx.document.getTempoMap());
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

        g.setColour (t.id == ctx.state.selectedTrackId ? Theme::lane.brighter (0.06f) : (i % 2 ? Theme::laneAlt : Theme::lane));
        g.fillRect (row);
        TimeGrid::drawGrid (g, row, axis, map, &ctx.state.grid);
        g.setColour (Theme::background);
        g.drawHorizontalLine (row.getBottom() - 1, 0.0f, (float) getWidth());

        const auto colour = Theme::parseColour (t.color);

        for (auto& c : t.midiClips)
        {
            const float x1 = (float) axis.tickToX ((double) c.startTick);
            const float x2 = (float) axis.tickToX ((double) c.endTick());

            if (x2 < 0 || x1 > (float) getWidth())
                continue;

            paintMidiClip (g, c, juce::Rectangle<float> (x1, (float) row.getY() + 3.0f, x2 - x1, (float) row.getHeight() - 7.0f),
                           colour, ctx.state.isClipSelected (c.id));
        }

        for (auto& c : t.audioClips)
        {
            const float x1 = (float) axis.tickToX ((double) c.startTick);
            const float x2 = (float) axis.tickToX ((double) collab::audioClipEndTick (c, map));

            if (x2 < 0 || x1 > (float) getWidth())
                continue;

            paintAudioClip (g, c, juce::Rectangle<float> (x1, (float) row.getY() + 3.0f, juce::jmax (2.0f, x2 - x1), (float) row.getHeight() - 7.0f),
                            colour, ctx.state.isClipSelected (c.id));
        }
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
    // 不透明（重なったとき、上の新しいテイクで下が隠れる。Pro Tools と同じく鳴るのも上だけ）
    const auto body = Theme::clipBody (colour);
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
        AudioFiles::drawWaveform (g, *thumb, wave, start, end, juce::Decibels::decibelsToGain ((float) c.gainDb),
                                  Theme::clipWave (colour));
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

    if (ctx.state.tool != EditTool::select && ctx.state.tool != EditTool::pencil)
        return setMouseCursor (Theme::toolCursor (ctx.state.tool));

    switch (hit.zone)
    {
        case Zone::leftEdge:
        case Zone::rightEdge:  setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case Zone::fadeIn:
        case Zone::fadeOut:    setMouseCursor (juce::MouseCursor::CrosshairCursor); break;
        case Zone::none:       setMouseCursor (ctx.state.pencil() ? Theme::pencilCursor() : juce::MouseCursor::NormalCursor); break;
        case Zone::body:       setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void TrackLanes::mouseExit (const juce::MouseEvent&)
{
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
        const auto minLen = juce::jmax<collab::Tick> (1, ctx.state.timelineGrid.stepTicks());
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
        const auto minLen = juce::jmax<collab::Tick> (1, ctx.state.timelineGrid.stepTicks());
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
                updated = collab::trimAudioClipStart (orig, snap ((double) orig.startTick + delta, e.mods), map);
                break;

            case DragMode::trimEnd:
            {
                const auto sourceLength = ctx.audioCache.getLengthSamples (ctx.document.getProjectDir(), orig.audioHash);
                updated = collab::trimAudioClipEnd (orig, snap ((double) collab::audioClipEndTick (orig, map) + delta, e.mods),
                                                    sourceLength, map);
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

            case DragMode::none:
            case DragMode::move:
            case DragMode::resizeMidi:
            case DragMode::trimMidiStart:
            case DragMode::rubberBand:
                break;
        }

        editClip (dragMode == DragMode::fadeIn || dragMode == DragMode::fadeOut ? "フェード"_ju : "トリム"_ju,
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

    const auto fromId = dragTrackId;
    const bool audio = dragAudio;

    ctx.document.perform ("クリップの移動"_ju, [fromId, targetTrackId, clipId, newStart, audio] (collab::Project& p)
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

//==============================================================================
TimelineView::TimelineView (AppContext& c)
    : ctx (c),
      ruler (c.document, c.state, c.state.timeline),
      tempoLane (c.document, c.state),
      meterLane (c.document, c.state),
      keyLane (c),
      chordLane (c),
      markerLane (c),
      lanes (c),
      playhead (c.state.timeline)
{
    addAndMakeVisible (ruler);
    addAndMakeVisible (lanes);

    // 上の段はトラックの行の上に置き、トラックと一緒に縦にスクロールする（固定しないので、トラックの場所を広く使える）
    lanes.topInset = topLanesHeight;

    for (auto* lane : topLanes())
    {
        lanes.addAndMakeVisible (lane);
        lane->addMouseListener (this, false);   // 乗っている段を明るくする・何もない所のクリックで再生位置
    }

    addAndMakeVisible (headerHolder);

    // ヘッダーとレーンの境目をドラッグしてヘッダーの幅を変える
    headerResizer.onResize = [this] (int dx)
    {
        if (headerWidthAtDrag == 0)
            headerWidthAtDrag = headerWidth;

        setHeaderWidth (headerWidthAtDrag + dx);
    };
    headerResizer.onResizeEnd = [this]
    {
        headerWidthAtDrag = 0;

        if (onHeaderWidthChanged)
            onHeaderWidthChanged();
    };
    addAndMakeVisible (headerResizer);
    headerHolder.addAndMakeVisible (laneHeaders);
    laneHeaders.addMouseListener (this, false);
    updateChordControls();
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);
    addAndMakeVisible (playhead);

    ruler.onSeek = [this] (double tick, const juce::ModifierKeys& mods)
    {
        ctx.engine.setPositionTick (ctx.state.snapCursor (tick, ctx.document.getTempoMap(), mods));
    };
    ruler.onWheel = [this] (auto& e, auto& w) { handleWheel (e, w); };
    lanes.onWheel = [this] (auto& e, auto& w) { handleWheel (e, w); };
    lanes.onOpenClip = [this] { if (onOpenClip) onOpenClip(); };

    hScroll.addListener (this);
    vScroll.addListener (this);
    hScroll.setAutoHide (false);
    vScroll.setAutoHide (false);

    ctx.document.addChangeListener (this);
    ctx.state.addChangeListener (this);
    rebuildHeaders();
}

TimelineView::~TimelineView()
{
    ctx.document.removeChangeListener (this);
    ctx.state.removeChangeListener (this);
}

void TimelineView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (16.5f));

    const int w = effectiveHeaderWidth() - 12;
    g.drawText ("小節"_ju, 12, 0, w, rulerHeight, juce::Justification::centredLeft);

    g.setColour (Theme::background);
    g.drawVerticalLine (effectiveHeaderWidth() - 1, 0.0f, (float) getHeight());
    g.drawHorizontalLine (rulerHeight - 1, 0.0f, (float) getWidth());
}

juce::Component* TimelineView::laneForKey (const std::string& key) const
{
    if (key == "meter")  return const_cast<MeterLane*> (&meterLane);
    if (key == "tempo")  return const_cast<TempoLane*> (&tempoLane);
    if (key == "key")    return const_cast<KeyLane*> (&keyLane);
    if (key == "chord")  return const_cast<ChordLane*> (&chordLane);
    if (key == "marker") return const_cast<MarkerLane*> (&markerLane);
    return nullptr;
}

juce::String TimelineView::laneTitle (const std::string& key)
{
    if (key == "meter")  return "拍子"_ju;
    if (key == "tempo")  return "テンポ"_ju;
    if (key == "key")    return "キー"_ju;
    if (key == "chord")  return "コード"_ju;
    return "マーカー"_ju;
}

juce::Colour TimelineView::laneColour (const std::string& key)
{
    if (key == "meter")  return Theme::meter;
    if (key == "tempo")  return Theme::tempo;
    if (key == "key")    return juce::Colour (0xff9ccc65);
    if (key == "chord")  return juce::Colour (0xffffb74d);
    return juce::Colour (0xff4dd0e1);
}

std::vector<juce::Component*> TimelineView::topLanes() const
{
    std::vector<juce::Component*> result;

    for (auto& key : ctx.state.laneOrder)
        if (auto* c = laneForKey (key))
            result.push_back (c);

    return result;
}

void TimelineView::layoutTopLanes()
{
    // 上の段: トラックの行と同じくスクロールに合わせて動かす
    int y = -lanes.scrollY;

    for (auto& key : ctx.state.laneOrder)
        if (auto* c = laneForKey (key))
        {
            const int h = key == "chord" ? chordLaneHeight : laneHeight;
            c->setBounds (0, y, lanes.getWidth(), h);
            y += h;
        }

    laneHeaders.setBounds (0, -lanes.scrollY, headerHolder.getWidth(), topLanesHeight);
    repaint();
}

void TimelineView::mouseMove (const juce::MouseEvent& e)
{
    auto lanes2 = topLanes();
    auto* over = std::find (lanes2.begin(), lanes2.end(), e.eventComponent) != lanes2.end() ? e.eventComponent : nullptr;

    // 見出しの列の上でも、その高さの段を明るくする
    if (over == nullptr && e.eventComponent == &laneHeaders)
        for (auto* lane : lanes2)
            if (e.y >= lane->getY() + lanes.scrollY && e.y < lane->getBottom() + lanes.scrollY)
                over = lane;

    if (over != hoveredLane)
    {
        hoveredLane = over;
        repaint();
    }
}

void TimelineView::mouseExit (const juce::MouseEvent& e)
{
    if (hoveredLane != nullptr && (e.eventComponent == hoveredLane || e.eventComponent == &laneHeaders))
    {
        hoveredLane = nullptr;
        repaint();
    }
}

void TimelineView::paintOverChildren (juce::Graphics& g)
{
    // 上の段の区切り線と、マウスのある段の明るさ（見出しから右端まで）。トラックの場所の中だけに描く
    g.reduceClipRegion (0, lanes.getY(), lanes.getRight(), lanes.getHeight());

    for (auto* lane : topLanes())
    {
        const auto row = juce::Rectangle<int> (0, lanes.getY() + lane->getY(), lanes.getX() + lane->getRight(), lane->getHeight());

        if (lane == hoveredLane)
        {
            g.setColour (Theme::overlay (0.07f));
            g.fillRect (row);
        }

        g.setColour (Theme::background);
        g.fillRect (row.getX(), row.getBottom() - 1, row.getWidth(), 1);
    }

    if (banding)
    {
        g.setColour (Theme::accent.withAlpha (0.15f));
        g.fillRect (band);
        g.setColour (Theme::accent.withAlpha (0.8f));
        g.drawRect (band, 1);
    }
}

int TimelineView::getTopAreaHeight() const
{
    return rulerHeight + topLanesHeight;
}

void TimelineView::setTopOnly (bool on, int leftWidth)
{
    if (on == topOnly && leftWidth == topOnlyLeft)
        return;

    topOnly = on;
    topOnlyLeft = juce::jmax (40, leftWidth);
    resized();
    repaint();
}

void TimelineView::setHeaderWidth (int w)
{
    w = juce::jlimit (minHeaderWidth, maxHeaderWidth, w);

    if (w != headerWidth)
    {
        headerWidth = w;
        resized();
        repaint();
    }
}

void TimelineView::resized()
{
    auto area = getLocalBounds();
    auto right = area.removeFromRight (scrollBarSize);
    vScroll.setBounds (right.withTrimmedTop (rulerHeight).withTrimmedBottom (scrollBarSize));
    vScroll.setVisible (! topOnly);
    hScroll.setVisible (! topOnly);
    headerResizer.setVisible (! topOnly);

    if (topOnly && lanes.scrollY != 0)
    {
        lanes.scrollY = 0;   // 上の段は一番上に固定
        vScroll.setCurrentRangeStart (0.0, juce::dontSendNotification);
    }

    auto left = area.removeFromLeft (effectiveHeaderWidth());
    auto bottom = topOnly ? juce::Rectangle<int>() : area.removeFromBottom (scrollBarSize);
    hScroll.setBounds (bottom);

    ruler.setBounds (area.removeFromTop (rulerHeight));
    lanes.setBounds (area);
    left.removeFromTop (rulerHeight);
    headerHolder.setBounds (left.withTrimmedRight (1));
    headerResizer.setBounds (left.getRight() - 3, 0, 6, getHeight());
    headerResizer.toFront (false);

    playhead.setBounds (ruler.getX(), ruler.getY(), ruler.getWidth(), lanes.getBottom() - ruler.getY());
    playhead.refresh();

    layoutHeaders();
    updateScrollBars();
}

void TimelineView::rebuildHeaders()
{
    const auto& tracks = ctx.document.getProject().tracks;
    bool same = (int) tracks.size() == headers.size();

    for (int i = 0; same && i < headers.size(); ++i)
        same = headers[i]->getTrackId() == tracks[(size_t) i].id;

    if (! same)
    {
        headers.clear();

        for (auto& t : tracks)
        {
            auto* h = headers.add (new TrackHeader (ctx, t.id));

            // ドラッグで並べ替え（ヘッダー自身のマウス処理の中で作り直さないよう、後で行う）
            h->onReorderDrop = [this] (const std::string& id, int y)
            {
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TimelineView> (this), id, y]
                {
                    if (safe != nullptr)
                        safe->moveTrackTo (id, y);
                });
            };

            headerHolder.addAndMakeVisible (h);
        }
    }

    for (auto* h : headers)
        h->update();

    layoutHeaders();
}

void TimelineView::moveTrackTo (const std::string& trackId, int y)
{
    const auto& tracks = ctx.document.getProject().tracks;
    const int from = ctx.document.getProject().indexOfTrack (trackId);
    int to = lanes.rowAt ((float) y);

    if (to < 0)
        to = y < 0 ? 0 : (int) tracks.size() - 1;

    if (from < 0 || from == to)
        return layoutHeaders();

    ctx.document.perform ("トラックの並べ替え"_ju, [trackId, to] (collab::Project& p)
    {
        const int i = p.indexOfTrack (trackId);

        if (i < 0)
            return;

        auto t = p.tracks[(size_t) i];
        p.tracks.erase (p.tracks.begin() + i);
        p.tracks.insert (p.tracks.begin() + juce::jlimit (0, (int) p.tracks.size(), to), t);
    });

    layoutHeaders();
}

void TimelineView::layoutHeaders()
{
    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, lanes.rowTop (i) - lanes.scrollY, headerHolder.getWidth(), lanes.rowHeightAt (i));

    layoutTopLanes();
}

void TimelineView::updateScrollBars()
{
    const auto& map = ctx.document.getTempoMap();
    const auto end = (double) map.barToTick (map.tickToBar (ctx.document.getProject().contentEndTick()) + 32);
    const double visible = lanes.getWidth() / ctx.state.timeline.pixelsPerTick();

    hScroll.setRangeLimits (0.0, juce::jmax (end, ctx.state.timeline.scrollTick + visible));
    hScroll.setCurrentRange (ctx.state.timeline.scrollTick, visible, juce::dontSendNotification);

    const int contentHeight = lanes.getContentHeight();
    vScroll.setRangeLimits (0.0, juce::jmax (contentHeight, lanes.getHeight()));
    lanes.scrollY = juce::jlimit (0, juce::jmax (0, contentHeight - lanes.getHeight()), lanes.scrollY);
    vScroll.setCurrentRange (lanes.scrollY, lanes.getHeight(), juce::dontSendNotification);
}

void TimelineView::scrollBarMoved (juce::ScrollBar* bar, double newStart)
{
    if (bar == &hScroll)
    {
        ctx.state.timeline.scrollTick = juce::jmax (0.0, newStart);
        stopFollowing();
        ctx.state.changed();
    }
    else
    {
        lanes.scrollY = (int) newStart;
        layoutHeaders();
        lanes.repaint();
    }
}

void TimelineView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    handleWheel (e, w);
}

void TimelineView::handleWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto& axis = ctx.state.timeline;

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        axis.zoomAround (e.getEventRelativeTo (&lanes).position.x, w.deltaY > 0 ? 1.15 : 1.0 / 1.15);
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        axis.scrollTick = juce::jmax (0.0, axis.scrollTick - d * 400.0 / axis.pixelsPerTick());
        stopFollowing();
    }
    else if (topOnly)
    {
        return;   // 上の段だけのときは縦に動かさない
    }
    else
    {
        lanes.scrollY = juce::jmax (0, lanes.scrollY - (int) (w.deltaY * 200.0f));
        updateScrollBars();
        layoutHeaders();
        lanes.repaint();
        return;
    }

    ctx.state.changed();
}

void TimelineView::mouseUp (const juce::MouseEvent& e)
{
    auto lanes2 = topLanes();

    if (banding)
    {
        // 矩形にかかった段と時間の範囲のコード・マーカーを選ぶ
        banding = bandCandidate = false;
        const auto& axis = ctx.state.timeline;
        const double t0 = axis.xToTick (band.getX() - lanes.getX()), t1 = axis.xToTick (band.getRight() - lanes.getX());
        const auto chordRow = chordLane.getBounds().translated (lanes.getX(), lanes.getY());
        const auto markerRow = markerLane.getBounds().translated (lanes.getX(), lanes.getY());
        auto& st = ctx.state;

        if (! e.mods.isShiftDown())
        {
            st.rangeChordIds.clear();
            st.rangeMarkerIds.clear();
        }

        const auto& project = ctx.document.getProject();

        if (band.getY() < chordRow.getBottom() && band.getBottom() > chordRow.getY())
            for (auto& c : project.chordTrack.events)
                if ((double) c.tick >= t0 && (double) c.tick <= t1)
                    st.rangeChordIds.insert (c.id);

        if (band.getY() < markerRow.getBottom() && band.getBottom() > markerRow.getY())
            for (auto& m : project.markerTrack.events)
                if ((double) m.tick >= t0 && (double) m.tick <= t1)
                    st.rangeMarkerIds.insert (m.id);

        st.selectedChordId = {};
        st.selectedMarkerId = {};
        st.changed();
        chordLane.grabKeyboardFocus();   // Ctrl+C / Delete をすぐ使えるように
        repaint();
        return;
    }

    bandCandidate = false;

    if (std::find (lanes2.begin(), lanes2.end(), e.eventComponent) == lanes2.end()
        || ctx.state.tool != EditTool::select || e.mods.isPopupMenu() || e.mouseWasDraggedSinceMouseDown())
        return;

    // その段で何も選ばれていない（＝何もない所をクリックした）ときだけ
    const auto& s = ctx.state;
    const bool nothing = (e.eventComponent == &tempoLane  && s.selectedTempoId.empty())
                      || (e.eventComponent == &meterLane  && s.selectedMeterId.empty())
                      || (e.eventComponent == &keyLane    && s.selectedKeyId.empty())
                      || (e.eventComponent == &chordLane  && s.selectedChordId.empty())
                      || (e.eventComponent == &markerLane && s.selectedMarkerId.empty());

    if (nothing && ctx.state.hasRangeSelection())
    {
        ctx.state.rangeChordIds.clear();
        ctx.state.rangeMarkerIds.clear();
        ctx.state.changed();
    }

    if (nothing)
        ctx.engine.setPositionTick (ctx.state.snapCursor (ctx.state.timeline.xToTick (e.position.x), ctx.document.getTempoMap(), e.mods));
}

void TimelineView::stopFollowing()
{
    // 再生中に手で横に動かしたら、自動スクロールをやめてその位置のままにする（前の小節を見たいときなど）
    if (ctx.state.autoScroll && ctx.engine.isPlaying())
        ctx.state.autoScroll = false;
}

void TimelineView::updateChordControls()
{
    const auto& pb = ctx.document.getProject().chordTrack.playback;
    laneHeaders.chordMute.setToggleState (! pb.enabled, juce::dontSendNotification);
}

void TimelineView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &ctx.document)
    {
        rebuildHeaders();
        updateChordControls();
    }
    else
    {
        for (auto* h : headers)
            h->update();   // 選択・ロック・録音待機の表示

        layoutHeaders();   // トラックの高さ
    }

    updateScrollBars();
    lanes.repaint();
    playhead.refresh();
}

bool TimelineView::deleteLaneSelection()
{
    if (deleteRange())
        return true;

    if (tempoLane.hasKeyboardFocus (false))  return tempoLane.deleteSelected();
    if (meterLane.hasKeyboardFocus (false))  return meterLane.deleteSelected();
    if (keyLane.hasKeyboardFocus (false))    return keyLane.deleteSelected();
    if (chordLane.hasKeyboardFocus (false))  return chordLane.deleteSelected();
    if (markerLane.hasKeyboardFocus (false)) return markerLane.deleteSelected();
    return false;
}

void TimelineView::setPlayheadTick (double tick)
{
    playhead.setTick (tick);
}

void TimelineView::followPlayhead (double tick)
{
    auto& axis = ctx.state.timeline;
    const double visible = lanes.getWidth() / axis.pixelsPerTick();

    if (tick < axis.scrollTick || tick > axis.scrollTick + visible * 0.95)
    {
        axis.scrollTick = juce::jmax (0.0, tick - visible * 0.05);
        ctx.state.changed();
    }
}

void TimelineView::mouseDown (const juce::MouseEvent& e)
{
    // 左上を右クリックしてもトラックを追加できる
    if (e.eventComponent == this && e.mods.isPopupMenu() && e.x < effectiveHeaderWidth() && ctx.addTrackMenu)
        ctx.addTrackMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());

    // 上の段の何もない所（段が何も選ばなかった所）で押したら、範囲選択の始まりかもしれない
    auto lanes2 = topLanes();
    bandCandidate = false;

    if (std::find (lanes2.begin(), lanes2.end(), e.eventComponent) == lanes2.end() || e.mods.isPopupMenu()
        || ctx.state.tool != EditTool::select)
        return;

    const auto& st = ctx.state;
    const bool nothing = (e.eventComponent == &tempoLane  && st.selectedTempoId.empty())
                      || (e.eventComponent == &meterLane  && st.selectedMeterId.empty())
                      || (e.eventComponent == &keyLane    && st.selectedKeyId.empty())
                      || (e.eventComponent == &chordLane  && st.selectedChordId.empty())
                      || (e.eventComponent == &markerLane && st.selectedMarkerId.empty());

    if (! nothing)
    {
        // 範囲選択の中のものを押したら範囲はそのまま、外なら範囲選択をやめる
        const bool inRange = (e.eventComponent == &chordLane && st.rangeChordIds.count (st.selectedChordId) > 0)
                          || (e.eventComponent == &markerLane && st.rangeMarkerIds.count (st.selectedMarkerId) > 0);

        if (! inRange && ctx.state.hasRangeSelection())
        {
            ctx.state.rangeChordIds.clear();
            ctx.state.rangeMarkerIds.clear();
            ctx.state.changed();
        }

        return;
    }

    bandCandidate = true;
    bandStart = e.getEventRelativeTo (this).getPosition();
}

void TimelineView::mouseDrag (const juce::MouseEvent& e)
{
    if (! bandCandidate)
        return;

    const auto p = e.getEventRelativeTo (this).getPosition();

    if (! banding && p.getDistanceFrom (bandStart) < 5)
        return;

    banding = true;
    band = juce::Rectangle<int> (bandStart, p);
    repaint();
}

void TimelineView::HeaderArea::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && ctx.addTrackMenu)
        ctx.addTrackMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

//==============================================================================
TimelineView::LaneHeaders::LaneHeaders (TimelineView& o) : owner (o)
{
    setTooltip ("並べ替え"_ju);

    // コードトラックのミュート（内蔵ピアノで鳴らすか。音量はミキサーのコードのストリップ）
    chordMute.setButtonText ("M");
    chordMute.setTooltip ("ミュート"_ju);
    chordMute.setWantsKeyboardFocus (false);
    chordMute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe57373));
    chordMute.onClick = [this]
    {
        const bool mute = owner.ctx.document.getProject().chordTrack.playback.enabled;
        owner.ctx.document.perform ("コードトラックのミュート"_ju, [mute] (collab::Project& p) { p.chordTrack.playback.enabled = ! mute; });
    };
    addAndMakeVisible (chordMute);
}

int TimelineView::LaneHeaders::indexAt (int y) const
{
    int top = 0, i = 0;

    for (auto& key : owner.ctx.state.laneOrder)
    {
        const int h = key == "chord" ? chordLaneHeight : laneHeight;

        if (y < top + h / 2)
            return i;

        top += h;
        ++i;
    }

    return i;
}

void TimelineView::LaneHeaders::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    int y = 0;
    int i = 0;
    const int w = getWidth() - 12;

    for (auto& key : owner.ctx.state.laneOrder)
    {
        const int h = key == "chord" ? chordLaneHeight : laneHeight;

        if (i == dragIndex)
        {
            g.setColour (Theme::overlay (0.08f));
            g.fillRect (0, y, getWidth(), h);
        }

        g.setColour (laneColour (key));
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (laneTitle (key), 12, y, w, h, juce::Justification::centredLeft);

        y += h;
        ++i;
    }

    // ドラッグ中: 入る場所の線
    if (dragIndex >= 0 && dropIndex >= 0)
    {
        int lineY = 0, k = 0;

        for (auto& key : owner.ctx.state.laneOrder)
        {
            if (k++ == dropIndex)
                break;

            lineY += key == "chord" ? chordLaneHeight : laneHeight;
        }

        g.setColour (Theme::accent);
        g.fillRect (0, juce::jlimit (0, getHeight() - 2, lineY - 1), getWidth(), 2);
    }
}

void TimelineView::LaneHeaders::resized()
{
    int y = 0;
    chordMute.setVisible (getWidth() >= 110);   // 狭いとき（ピアノロールの全画面）は M を出さない

    for (auto& key : owner.ctx.state.laneOrder)
    {
        const int h = key == "chord" ? chordLaneHeight : laneHeight;

        if (key == "chord")
            chordMute.setBounds (juce::Rectangle<int> (getWidth() - 38, y, 30, h).reduced (0, 8));

        y += h;
    }
}

void TimelineView::LaneHeaders::mouseMove (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);   // 見出しをドラッグで並べ替えられる（印は出さない）
}

void TimelineView::LaneHeaders::mouseDown (const juce::MouseEvent& e)
{
    // 押した段（見出しの行）を覚えておく
    int top = 0, k = 0;
    dragIndex = -1;

    for (auto& key : owner.ctx.state.laneOrder)
    {
        top += key == "chord" ? chordLaneHeight : laneHeight;

        if (e.y < top)
        {
            dragIndex = k;
            break;
        }

        ++k;
    }

    if (e.mods.isPopupMenu())
        dragIndex = -1;

    dropIndex = -1;
    repaint();
}

void TimelineView::LaneHeaders::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0 || e.getDistanceFromDragStart() < 4)
        return;

    dropIndex = indexAt (e.y);
    repaint();
}

void TimelineView::LaneHeaders::mouseUp (const juce::MouseEvent&)
{
    auto& order = owner.ctx.state.laneOrder;

    if (dragIndex >= 0 && dropIndex >= 0 && dropIndex != dragIndex && dropIndex != dragIndex + 1)
    {
        const auto key = order[(size_t) dragIndex];
        order.erase (order.begin() + dragIndex);
        const int to = dropIndex > dragIndex ? dropIndex - 1 : dropIndex;
        order.insert (order.begin() + juce::jlimit (0, (int) order.size(), to), key);

        if (owner.onLaneOrderChanged)
            owner.onLaneOrderChanged();

        resized();
        owner.layoutTopLanes();
        owner.ctx.state.changed();
    }

    dragIndex = dropIndex = -1;
    repaint();
}

//==============================================================================
namespace
{
    // 範囲選択のクリップボード（位置は先頭からの相対）
    struct RangeClipboard
    {
        std::vector<collab::ChordEvent> chords;
        std::vector<collab::Marker> markers;
    };

    std::optional<RangeClipboard> rangeClipboard;
}

bool TimelineView::hasRangeClipboard()
{
    return rangeClipboard.has_value();
}

bool TimelineView::copyRange (bool cut)
{
    auto& st = ctx.state;

    if (! st.hasRangeSelection())
        return false;

    const auto& project = ctx.document.getProject();
    RangeClipboard clip;
    collab::Tick origin = std::numeric_limits<collab::Tick>::max();

    for (auto& c : project.chordTrack.events)
        if (st.rangeChordIds.count (c.id) > 0)
        {
            clip.chords.push_back (c);
            origin = std::min (origin, c.tick);
        }

    for (auto& m : project.markerTrack.events)
        if (st.rangeMarkerIds.count (m.id) > 0)
        {
            clip.markers.push_back (m);
            origin = std::min (origin, m.tick);
        }

    // 小節の頭を基準にする（貼り付けた先でも小節の中の位置が同じになるように）
    const auto& map = ctx.document.getTempoMap();
    origin = map.barToTick (map.tickToBar (origin));

    for (auto& c : clip.chords)  c.tick -= origin;
    for (auto& m : clip.markers) m.tick -= origin;

    rangeClipboard = clip;

    if (cut)
        deleteRange();

    return true;
}

bool TimelineView::pasteRange (double playheadTick)
{
    if (! rangeClipboard)
        return false;

    // 再生位置の小節の頭に貼る
    const auto& map = ctx.document.getTempoMap();
    const auto at = map.barToTick (map.tickToBar ((collab::Tick) std::llround (juce::jmax (0.0, playheadTick))));
    auto clip = *rangeClipboard;
    auto& st = ctx.state;
    st.rangeChordIds.clear();
    st.rangeMarkerIds.clear();

    for (auto& c : clip.chords)
    {
        c.tick += at;
        c.id = collab::generateUuid();
        st.rangeChordIds.insert (c.id);
    }

    for (auto& m : clip.markers)
    {
        m.tick += at;
        m.id = collab::generateUuid();
        st.rangeMarkerIds.insert (m.id);
    }

    ctx.document.perform ("コード・マーカーの貼り付け"_ju, [clip] (collab::Project& p)
    {
        // 同じ拍にあるコードは置き換える
        for (auto& c : clip.chords)
        {
            auto& ev = p.chordTrack.events;
            std::erase_if (ev, [tick = c.tick] (const collab::ChordEvent& x) { return x.tick == tick; });
            ev.push_back (c);
        }

        if (! clip.markers.empty() && p.markerTrack.id.empty())
            p.markerTrack.id = collab::markerTrackIdFor (p.projectId);

        for (auto& m : clip.markers)
            p.markerTrack.events.push_back (m);
    });

    st.changed();
    return true;
}

bool TimelineView::deleteRange()
{
    auto& st = ctx.state;

    if (! st.hasRangeSelection())
        return false;

    const auto chords = st.rangeChordIds;
    const auto markers = st.rangeMarkerIds;

    ctx.document.perform ("コード・マーカーの削除"_ju, [chords, markers] (collab::Project& p)
    {
        auto& ev = p.chordTrack.events;
        std::erase_if (ev, [&chords] (const collab::ChordEvent& x) { return chords.count (x.id) > 0; });
        auto& mk = p.markerTrack.events;
        std::erase_if (mk, [&markers] (const collab::Marker& x) { return markers.count (x.id) > 0; });
    });

    st.rangeChordIds.clear();
    st.rangeMarkerIds.clear();
    st.changed();
    return true;
}
