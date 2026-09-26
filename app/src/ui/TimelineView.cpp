#include "TimelineView.h"

#include "TimeGrid.h"
#include "collab/Uuid.h"

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
    setTooltip ("ダブルクリック: クリップを作成／ピアノロールで開く　ドラッグ: 移動　右端: 長さ変更　Alt: スナップなし"_ju);
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

    for (auto it = track.midiClips.rbegin(); it != track.midiClips.rend(); ++it)
    {
        const float x1 = (float) axis.tickToX ((double) it->startTick);
        const float x2 = (float) axis.tickToX ((double) it->endTick());

        if (p.x >= x1 && p.x <= x2)
        {
            hit.clipId = it->id;
            hit.nearRightEdge = x2 - p.x <= edgeGrab && x2 - x1 > edgeGrab * 2;
            break;
        }
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
        g.drawText ("左下の「+ トラックを追加」からトラックを作成してください"_ju, getLocalBounds(), juce::Justification::centred);
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

void TrackLanes::mouseMove (const juce::MouseEvent& e)
{
    auto hit = findHit (e.position);
    setMouseCursor (hit.nearRightEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void TrackLanes::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    auto hit = findHit (e.position);
    const auto& project = ctx.document.getProject();
    dragMode = DragMode::none;

    if (hit.trackIndex < 0)
    {
        ctx.state.selectedClipId = {};
        ctx.state.changed();
        return;
    }

    const auto& track = project.tracks[(size_t) hit.trackIndex];
    ctx.state.selectedTrackId = track.id;
    ctx.state.selectedClipId = hit.clipId;
    ctx.state.changed();

    if (hit.clipId.empty())
        return;

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        auto trackId = track.id, clipId = hit.clipId;
        m.addItem ("ピアノロールで開く"_ju, [this] { if (onOpenClip) onOpenClip(); });
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
        m.addItem ("削除"_ju, [this, trackId, clipId]
        {
            ctx.document.perform ("クリップの削除"_ju, [trackId, clipId] (collab::Project& p)
            {
                if (auto* t = p.findTrack (trackId))
                    t->midiClips.erase (std::remove_if (t->midiClips.begin(), t->midiClips.end(),
                                                        [&] (auto& c) { return c.id == clipId; }), t->midiClips.end());
            });
        });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        return;
    }

    if (auto* clip = track.findMidiClip (hit.clipId))
    {
        dragMode = hit.nearRightEdge ? DragMode::resize : DragMode::move;
        dragTrackId = track.id;
        dragClipId = clip->id;
        dragOrigStart = clip->startTick;
        dragOrigLength = clip->lengthTick;
        dragDownTick = ctx.state.timeline.xToTick (e.position.x);
        mergeId = juce::Uuid().toString();
    }
}

void TrackLanes::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == DragMode::none || e.getDistanceFromDragStart() < 3)
        return;

    const double delta = ctx.state.timeline.xToTick (e.position.x) - dragDownTick;
    const auto clipId = dragClipId;

    if (dragMode == DragMode::resize)
    {
        const auto minLen = juce::jmax<collab::Tick> (1, ctx.state.timelineGrid.stepTicks());
        const auto end = juce::jmax (dragOrigStart + minLen, snap ((double) (dragOrigStart + dragOrigLength) + delta, e.mods));
        const auto trackId = dragTrackId;

        ctx.document.perform ("クリップの長さ変更"_ju, [trackId, clipId, end] (collab::Project& p)
        {
            if (auto* t = p.findTrack (trackId))
                if (auto* c = t->findMidiClip (clipId))
                    c->lengthTick = end - c->startTick;
        }, mergeId);
        return;
    }

    const auto newStart = snap ((double) dragOrigStart + delta, e.mods);
    const auto& project = ctx.document.getProject();
    auto targetTrackId = dragTrackId;

    // 別の MIDI トラックへの移動
    if (const int row = rowAt (e.position.y); row >= 0)
        if (project.tracks[(size_t) row].type == collab::TrackType::midi)
            targetTrackId = project.tracks[(size_t) row].id;

    const auto fromId = dragTrackId;

    ctx.document.perform ("クリップの移動"_ju, [fromId, targetTrackId, clipId, newStart] (collab::Project& p)
    {
        auto* from = p.findTrack (fromId);
        auto* to = p.findTrack (targetTrackId);

        if (from == nullptr || to == nullptr)
            return;

        auto it = std::find_if (from->midiClips.begin(), from->midiClips.end(), [&] (auto& c) { return c.id == clipId; });

        if (it == from->midiClips.end())
            return;

        it->startTick = newStart;

        if (from != to)
        {
            to->midiClips.push_back (*it);
            from->midiClips.erase (it);
        }
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
    ctx.document.endMerge();
}

void TrackLanes::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto hit = findHit (e.position);

    if (hit.trackIndex < 0)
        return;

    if (! hit.clipId.empty())
    {
        if (onOpenClip)
            onOpenClip();

        return;
    }

    const auto& track = ctx.document.getProject().tracks[(size_t) hit.trackIndex];

    if (track.type != collab::TrackType::midi)
        return;

    // クリックした小節の頭から 4 小節のクリップを作る
    const auto& map = ctx.document.getTempoMap();
    const int bar = map.tickToBar ((collab::Tick) juce::jmax (0.0, ctx.state.timeline.xToTick (e.position.x)));
    collab::MidiClip clip;
    clip.id = collab::generateUuid();
    clip.startTick = map.barToTick (bar);
    clip.lengthTick = map.barToTick (bar + 4) - clip.startTick;

    auto trackId = track.id;
    ctx.document.perform ("クリップの作成"_ju, [trackId, clip] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            t->midiClips.push_back (clip);
    });

    ctx.state.selectedTrackId = trackId;
    ctx.state.selectedClipId = clip.id;
    ctx.state.changed();

    if (onOpenClip)
        onOpenClip();
}

void TrackLanes::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (onWheel)
        onWheel (e, w);
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
    addAndMakeVisible (addTrackButton);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);
    addAndMakeVisible (playhead);

    ruler.onSeek = [this] (double tick) { ctx.engine.setPositionTick (tick); };
    ruler.onWheel = [this] (auto& e, auto& w) { handleWheel (e, w); };
    lanes.onWheel = [this] (auto& e, auto& w) { handleWheel (e, w); };
    lanes.onOpenClip = [this] { if (onOpenClip) onOpenClip(); };

    addTrackButton.onClick = [this] { showAddTrackMenu(); };

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
    addTrackButton.setBounds (left.removeFromBottom (scrollBarSize + 22).reduced (6, 2).withTrimmedBottom (scrollBarSize - 2));
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
            h->repaint();

    updateScrollBars();
    lanes.repaint();
    playhead.refresh();
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

void TimelineView::showAddTrackMenu()
{
    juce::PopupMenu m;
    m.addItem ("MIDIトラック（ドラム）"_ju, [this] { ctx.addBuiltinMidiTrack (collab::builtin::drums, "Drums"); });
    m.addItem ("MIDIトラック（ベース）"_ju, [this] { ctx.addBuiltinMidiTrack (collab::builtin::bass, "Bass"); });
    m.addItem ("MIDIトラック（ピアノ）"_ju, [this] { ctx.addBuiltinMidiTrack (collab::builtin::piano, "Piano"); });
    m.addSeparator();
    m.addItem ("オーディオトラック（M2 で対応）"_ju, false, false, nullptr);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackButton));
}
