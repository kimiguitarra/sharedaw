#include "TimelineView.h"

#include "TimeGrid.h"
#include "collab/Uuid.h"
#include "collab/ClipEditing.h"
#include "Dialogs.h"
#include "audio/AudioFiles.h"

namespace
{
    constexpr int rulerHeight = 26;
    constexpr int laneHeight = 24;
    constexpr int chordLaneHeight = 30;
    constexpr int topHeight = rulerHeight + laneHeight * 2 + chordLaneHeight;
    constexpr int scrollBarSize = 12;
    constexpr float edgeGrab = 7.0f;
}

//==============================================================================
TrackLanes::TrackLanes (AppContext& c) : ctx (c)
{
    setWantsKeyboardFocus (true);
    setTooltip ("鉛筆ツール: MIDI トラックをクリック（ドラッグで長さ）してクリップを作成　選択ツール: ドラッグで移動、端で長さ・トリム、上の角でフェード、ダブルクリックでピアノロール　Alt: スナップなし　オーディオはドラッグ＆ドロップで読み込み"_ju);
}

int TrackLanes::getContentHeight() const
{
    return (int) ctx.document.getProject().tracks.size() * rowHeight;
}

int TrackLanes::rowAt (float y) const
{
    const int row = (int) std::floor ((y + (float) scrollY) / (float) rowHeight);
    return row >= 0 && row < (int) ctx.document.getProject().tracks.size() ? row : -1;
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
    const float rowTop = (float) (hit.trackIndex * rowHeight - scrollY) + 3.0f;

    for (auto it = track.midiClips.rbegin(); it != track.midiClips.rend(); ++it)
    {
        const float x1 = (float) axis.tickToX ((double) it->startTick);
        const float x2 = (float) axis.tickToX ((double) it->endTick());

        if (p.x >= x1 && p.x <= x2)
        {
            hit.clipId = it->id;
            hit.zone = x2 - p.x <= edgeGrab && x2 - x1 > edgeGrab * 2 ? Zone::rightEdge : Zone::body;
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
        const bool nearTop = p.y - rowTop < 10.0f;

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
        const auto row = juce::Rectangle<int> (0, (int) i * rowHeight - scrollY, getWidth(), rowHeight);

        if (row.getBottom() < 0 || row.getY() > getHeight())
            continue;

        g.setColour (t.id == ctx.state.selectedTrackId ? Theme::lane.brighter (0.06f) : (i % 2 ? Theme::laneAlt : Theme::lane));
        g.fillRect (row);
        TimeGrid::drawGrid (g, row, axis, map, nullptr);
        g.setColour (Theme::background);
        g.drawHorizontalLine (row.getBottom() - 1, 0.0f, (float) getWidth());

        const auto colour = Theme::parseColour (t.color);

        for (auto& c : t.midiClips)
        {
            const float x1 = (float) axis.tickToX ((double) c.startTick);
            const float x2 = (float) axis.tickToX ((double) c.endTick());

            if (x2 < 0 || x1 > (float) getWidth())
                continue;

            paintMidiClip (g, c, juce::Rectangle<float> (x1, (float) row.getY() + 3.0f, x2 - x1, (float) rowHeight - 7.0f),
                           colour, c.id == ctx.state.selectedClipId);
        }

        for (auto& c : t.audioClips)
        {
            const float x1 = (float) axis.tickToX ((double) c.startTick);
            const float x2 = (float) axis.tickToX ((double) collab::audioClipEndTick (c, map));

            if (x2 < 0 || x1 > (float) getWidth())
                continue;

            paintAudioClip (g, c, juce::Rectangle<float> (x1, (float) row.getY() + 3.0f, juce::jmax (2.0f, x2 - x1), (float) rowHeight - 7.0f),
                            colour, c.id == ctx.state.selectedClipId);
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

    if (project.tracks.empty())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
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
        if (n.tick >= c.lengthTick)
            continue;

        const float x1 = (float) axis.tickToX ((double) (c.startTick + n.tick));
        const float x2 = (float) axis.tickToX ((double) juce::jmin (c.startTick + n.endTick(), c.endTick()));
        const float y = inner.getBottom() - ((float) (n.pitch - lo) + 0.5f) / range * inner.getHeight();
        g.fillRect (juce::Rectangle<float> (x1, y - 1.0f, juce::jmax (1.5f, x2 - x1), 2.5f));
    }
}

void TrackLanes::paintAudioClip (juce::Graphics& g, const collab::AudioClip& c, juce::Rectangle<float> r,
                                 juce::Colour colour, bool selected)
{
    g.setColour (colour.withAlpha (0.28f));
    g.fillRoundedRectangle (r, 3.0f);

    // 波形（元ファイルの offset 〜 offset + length の範囲）
    const auto gain = juce::Decibels::decibelsToGain ((float) c.gainDb);

    if (auto* thumb = ctx.audioCache.getThumbnail (ctx.document.getProjectDir(), c.audioHash))
    {
        g.setColour (colour.brighter (0.5f));
        const double start = (double) c.sourceOffsetSamples / collab::kSampleRate;
        const double end = start + (double) c.lengthSamples / collab::kSampleRate;
        thumb->drawChannels (g, r.reduced (1.0f, 12.0f).toNearestInt(), start, end, gain);
    }
    else
    {
        g.setColour (Theme::warning);
        g.setFont (juce::FontOptions (12.0f));
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

    g.setColour (selected ? Theme::selection : colour);
    g.drawRoundedRectangle (r, 3.0f, selected ? 2.0f : 1.0f);

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (11.0f));
    auto label = toJuce (c.displayName);

    if (std::abs (c.gainDb) > 0.05)
        label << "  " << juce::String (c.gainDb, 1) << " dB";

    g.drawText (label, r.reduced (6.0f, 0.0f).removeFromBottom (14.0f), juce::Justification::centredLeft, true);
}

void TrackLanes::mouseMove (const juce::MouseEvent& e)
{
    auto hit = findHit (e.position);

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
            ctx.state.selectedClipId = newId;
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
    dragMode = DragMode::none;
    dragTrackId = {};

    if (hit.trackIndex < 0)
    {
        ctx.state.selectedClipId = {};
        ctx.state.changed();

        // トラックの下の空いている所: トラックの追加
        if (e.mods.isPopupMenu() && ctx.addTrackMenu)
            ctx.addTrackMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));

        return;
    }

    const auto& track = project.tracks[(size_t) hit.trackIndex];
    ctx.state.selectedTrackId = track.id;
    ctx.state.selectedClipId = hit.clipId;
    ctx.state.changed();

    if (hit.clipId.empty())
    {
        if (track.type != collab::TrackType::midi)
            return;

        const auto bar = ctx.document.getTempoMap().tickToBar ((collab::Tick) juce::jmax (0.0, ctx.state.timeline.xToTick (e.position.x)));

        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem ("ここに MIDI クリップを作成"_ju, [this, trackId = track.id, bar] { createMidiClip (trackId, bar, false); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }
        else if (ctx.state.pencil())
        {
            // 鉛筆ツール: クリックした小節から 1 小節のクリップを作り、そのままドラッグで長さを決める
            createMidiClip (track.id, bar, true);
        }

        return;
    }

    if (e.mods.isPopupMenu())
        return showClipMenu (track, hit.clipId, hit.audio);

    dragTrackId = track.id;
    dragClipId = hit.clipId;
    dragAudio = hit.audio;
    dragDownTick = ctx.state.timeline.xToTick (e.position.x);
    mergeId = juce::Uuid().toString();

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
        dragMode = hit.zone == Zone::rightEdge ? DragMode::resizeMidi : DragMode::move;
        dragOrigStart = clip->startTick;
        dragOrigLength = clip->lengthTick;
    }
}

void TrackLanes::mouseDrag (const juce::MouseEvent& e)
{
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

    // 移動（同じ種類のトラックへなら、トラックをまたいで移動できる）
    const auto newStart = snap ((double) dragOrigStart + delta, e.mods);
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
    ctx.state.selectedClipId = clip.id;
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
        if (juce::File (f).hasFileExtension (AudioFiles::supportedWildcard().replace ("*", "")))
            return true;

    return false;
}

void TrackLanes::filesDropped (const juce::StringArray& paths, int x, int y)
{
    juce::Array<juce::File> files;

    for (auto& p : paths)
        files.add (juce::File (p));

    std::string trackId;

    if (const int row = rowAt ((float) y); row >= 0)
    {
        const auto& t = ctx.document.getProject().tracks[(size_t) row];

        if (t.type == collab::TrackType::audio)
            trackId = t.id;
    }

    const auto& map = ctx.document.getTempoMap();
    const auto tick = map.barToTick (map.tickToBar ((collab::Tick) juce::jmax (0.0, ctx.state.timeline.xToTick ((double) x))));

    // ドロップの処理中にモーダルな進捗表示を出さないよう、少し後で読み込む
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TrackLanes> (this), files, trackId, tick]
    {
        if (safe != nullptr)
            safe->ctx.importAudioFiles (files, trackId, tick);
    });
}

//==============================================================================
TimelineView::TimelineView (AppContext& c)
    : ctx (c),
      ruler (c.document, c.state, c.state.timeline),
      tempoLane (c.document, c.state),
      meterLane (c.document, c.state),
      chordLane (c),
      lanes (c),
      playhead (c.state.timeline)
{
    addAndMakeVisible (ruler);
    addAndMakeVisible (tempoLane);
    addAndMakeVisible (meterLane);
    addAndMakeVisible (chordLane);
    addAndMakeVisible (lanes);

    // コードトラックの発音（内蔵ピアノ）のオン・オフと音量
    chordPlaybackToggle.setButtonText ("発音"_ju);
    chordPlaybackToggle.setTooltip ("コードトラックを内蔵ピアノで鳴らす"_ju);
    chordPlaybackToggle.onClick = [this]
    {
        const bool on = chordPlaybackToggle.getToggleState();
        ctx.document.perform ("コードトラックの発音"_ju, [on] (collab::Project& p) { p.chordTrack.playback.enabled = on; });
    };
    addAndMakeVisible (chordPlaybackToggle);

    chordVolume.setRange (-40.0, 6.0, 0.1);
    chordVolume.setDoubleClickReturnValue (true, -6.0);
    chordVolume.setPopupDisplayEnabled (true, true, nullptr);
    chordVolume.setTextValueSuffix (" dB");
    chordVolume.setTooltip ("コードトラックの音量"_ju);
    chordVolume.onDragStart = [this] { chordVolumeMergeId = juce::Uuid().toString(); };
    chordVolume.onDragEnd = [this] { ctx.document.endMerge(); };
    chordVolume.onValueChange = [this]
    {
        const double v = chordVolume.getValue();
        ctx.document.perform ("コードトラックの音量"_ju, [v] (collab::Project& p) { p.chordTrack.playback.volumeDb = v; },
                              chordVolumeMergeId);
    };
    addAndMakeVisible (chordVolume);
    updateChordControls();
    addAndMakeVisible (headerHolder);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);
    addAndMakeVisible (playhead);

    ruler.onSeek = [this] (double tick) { ctx.engine.setPositionTick (tick); };
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
    g.setFont (juce::FontOptions (12.0f));

    const int w = headerWidth - 10;
    g.drawText ("小節"_ju, 10, 0, w, rulerHeight, juce::Justification::centredLeft);
    g.setColour (Theme::tempo);
    g.drawText ("テンポ"_ju, 10, rulerHeight, w, laneHeight, juce::Justification::centredLeft);
    g.setColour (Theme::meter);
    g.drawText ("拍子"_ju, 10, rulerHeight + laneHeight, w, laneHeight, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffffb74d));
    g.drawText ("コード"_ju, 10, rulerHeight + laneHeight * 2, w, chordLaneHeight, juce::Justification::centredLeft);

    g.setColour (Theme::background);
    g.drawVerticalLine (headerWidth - 1, 0.0f, (float) getHeight());
    g.drawHorizontalLine (topHeight - 1, 0.0f, (float) getWidth());
}

void TimelineView::resized()
{
    auto area = getLocalBounds();
    auto right = area.removeFromRight (scrollBarSize);
    vScroll.setBounds (right.withTrimmedTop (topHeight).withTrimmedBottom (scrollBarSize));

    auto left = area.removeFromLeft (headerWidth);
    auto bottom = area.removeFromBottom (scrollBarSize);
    hScroll.setBounds (bottom);

    ruler.setBounds (area.removeFromTop (rulerHeight));
    tempoLane.setBounds (area.removeFromTop (laneHeight));
    meterLane.setBounds (area.removeFromTop (laneHeight));
    chordLane.setBounds (area.removeFromTop (chordLaneHeight));
    lanes.setBounds (area);

    {
        auto chordRow = left.withTop (chordLane.getY()).withHeight (chordLaneHeight).reduced (4, 4);
        chordRow.removeFromLeft (56);
        chordPlaybackToggle.setBounds (chordRow.removeFromLeft (70));
        chordVolume.setBounds (chordRow);
    }

    left.removeFromTop (topHeight);
    headerHolder.setBounds (left.withTrimmedRight (1));

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
            headerHolder.addAndMakeVisible (headers.add (new TrackHeader (ctx, t.id)));
    }

    for (auto* h : headers)
        h->update();

    layoutHeaders();
}

void TimelineView::layoutHeaders()
{
    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, i * TrackLanes::rowHeight - lanes.scrollY, headerHolder.getWidth(), TrackLanes::rowHeight);
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
        ctx.state.changed();
    }
    else
    {
        lanes.scrollY = (int) newStart;
        layoutHeaders();
        lanes.repaint();
    }
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

void TimelineView::updateChordControls()
{
    const auto& pb = ctx.document.getProject().chordTrack.playback;
    chordPlaybackToggle.setToggleState (pb.enabled, juce::dontSendNotification);
    chordVolume.setValue (pb.volumeDb, juce::dontSendNotification);
}

void TimelineView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &ctx.document)
    {
        rebuildHeaders();
        updateChordControls();
    }
    else
        for (auto* h : headers)
            h->update();   // 選択・ロック・録音待機の表示

    updateScrollBars();
    lanes.repaint();
    playhead.refresh();
}

bool TimelineView::deleteLaneSelection()
{
    if (tempoLane.hasKeyboardFocus (false))  return tempoLane.deleteSelected();
    if (meterLane.hasKeyboardFocus (false))  return meterLane.deleteSelected();
    if (chordLane.hasKeyboardFocus (false))  return chordLane.deleteSelected();
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
    // 左上（テンポ・拍子・コードの見出し）を右クリックしてもトラックを追加できる
    if (e.mods.isPopupMenu() && e.x < headerWidth && ctx.addTrackMenu)
        ctx.addTrackMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void TimelineView::HeaderArea::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && ctx.addTrackMenu)
        ctx.addTrackMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}
