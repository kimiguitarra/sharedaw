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

    const int w = headerWidth - 12;
    g.drawText ("小節"_ju, 12, 0, w, rulerHeight, juce::Justification::centredLeft);

    g.setColour (Theme::background);
    g.drawVerticalLine (headerWidth - 1, 0.0f, (float) getHeight());
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
    if (key == "key")    return Theme::keyLane;
    if (key == "chord")  return Theme::chordLane;
    return Theme::markerLane;
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

    auto left = area.removeFromLeft (headerWidth);
    auto bottom = area.removeFromBottom (scrollBarSize);
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
    if (e.eventComponent == this && e.mods.isPopupMenu() && e.x < headerWidth && ctx.addTrackMenu)
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
