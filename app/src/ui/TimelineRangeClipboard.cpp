// タイムラインの上の段（コード・マーカー）の範囲選択のコピー・切り取り・貼り付け・削除

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
