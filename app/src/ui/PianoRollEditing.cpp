// ピアノロールのノートの編集（コピー・貼り付け・複製・削除・選択・クオンタイズ、クリップを用意する）

#include "PianoRoll.h"
#include "PianoRollDetail.h"

#include "TimeGrid.h"
#include "audio/AudioFiles.h"
#include "collab/ClipEditing.h"
#include "collab/GmDrumMap.h"
#include "collab/Uuid.h"

using namespace PianoRollDetail;

//==============================================================================
void PianoRollView::copySelectedNotes (bool cut)
{
    auto* clip = getClip();

    if (clip == nullptr || selectedNotes.empty())
        return;

    noteClipboard.clear();
    collab::Tick origin = std::numeric_limits<collab::Tick>::max();

    for (auto& n : clip->notes)
        if (selectedNotes.count (n.id) > 0)
        {
            noteClipboard.push_back (n);
            origin = std::min (origin, n.tick);
        }

    for (auto& n : noteClipboard)
        n.tick -= origin;   // いちばん左のノートを 0 に

    if (cut)
        deleteSelectedNotes();
}

void PianoRollView::pasteNotes()
{
    auto* clip = getClip();

    if (clip == nullptr || noteClipboard.empty())
        return;

    // 再生位置に貼る（Cubase と同じく位置は再生位置が基準）。クリップの外なら、貼ったノートが入るまでクリップを小節単位で広げる
    const auto& map = ctx.document.getTempoMap();
    const auto abs = (collab::Tick) std::llround (ctx.state.snapCursor (ctx.state.playheadTick, map, {}));

    // 再生位置に同じトラックの別のクリップがあれば、そちらに貼る
    if (auto* track = getTrack())
        for (auto& c : track->midiClips)
            if (c.id != clip->id && abs >= c.startTick && abs < c.endTick())
            {
                ctx.state.selectClip (c.id);
                ctx.state.changed();
                clip = getClip();
                break;
            }

    if (clip == nullptr)
        return;

    collab::Tick spanEnd = 0;
    for (auto& n : noteClipboard)
        spanEnd = std::max (spanEnd, n.endTick());

    const auto newStart = std::min (clip->startTick, map.barToTick (map.tickToBar (abs)));
    const auto lastEnd = abs + spanEnd;
    const auto newEnd = std::max (clip->endTick(), lastEnd > clip->endTick() ? map.barToTick (map.tickToBar (lastEnd - 1) + 1) : clip->endTick());
    const auto shift = clip->startTick - newStart;   // 前に広げた分だけ、今あるノートを後ろにずらす（曲の中の位置は同じ）

    auto notes = noteClipboard;
    std::set<std::string> ids;

    for (auto& n : notes)
    {
        n.id = collab::generateUuid();
        n.tick += abs - newStart;
        ids.insert (n.id);
    }

    editNotes ("ノートの貼り付け"_ju, [notes, newStart, newEnd, shift] (collab::MidiClip& c)
    {
        if (shift > 0)
            for (auto& n : c.notes)
                n.tick += shift;

        c.startTick = newStart;
        c.lengthTick = newEnd - newStart;
        c.notes.insert (c.notes.end(), notes.begin(), notes.end());
    });
    selectedNotes = ids;
    repaint();
}

void PianoRollView::duplicateSelectedNotes()
{
    auto* clip = getClip();

    if (clip == nullptr || selectedNotes.empty())
        return;

    // 選んだノートの範囲の直後に同じ並びで置く（Cubase の Ctrl+D）
    collab::Tick first = std::numeric_limits<collab::Tick>::max(), last = 0;

    for (auto& n : clip->notes)
        if (selectedNotes.count (n.id) > 0)
        {
            first = std::min (first, n.tick);
            last = std::max (last, n.endTick());
        }

    // 小節の途中で終わっていても、グリッドの単位で揃える
    const auto step = std::max<collab::Tick> (1, ctx.state.grid.stepTicks());
    const auto span = ((last - first + step - 1) / step) * step;
    std::vector<collab::Note> copies;
    std::set<std::string> ids;

    for (auto& n : clip->notes)
        if (selectedNotes.count (n.id) > 0)
        {
            auto c = n;
            c.id = collab::generateUuid();
            c.tick += span;
            copies.push_back (c);
            ids.insert (c.id);
        }

    editNotes ("ノートの複製"_ju, [copies] (collab::MidiClip& c)
    {
        c.notes.insert (c.notes.end(), copies.begin(), copies.end());

        // はみ出したらクリップを伸ばす
        for (auto& n : copies)
            c.lengthTick = std::max (c.lengthTick, n.endTick());
    });

    selectedNotes = ids;
    repaint();
}

void PianoRollView::focusEditor()
{
    grid.grabKeyboardFocus();
}

bool PianoRollView::canCreateClip() const
{
    auto* t = getTrack();
    return t != nullptr && t->type == collab::TrackType::midi;
}

const collab::MidiClip* PianoRollView::ensureClipAt (collab::Tick abs)
{
    auto* track = getTrack();

    if (track == nullptr || track->type != collab::TrackType::midi)
        return nullptr;

    // すでにクリップがある所ならそれを選ぶ
    for (auto& c : track->midiClips)
        if (abs >= c.startTick && abs < c.endTick())
        {
            if (c.id != ctx.state.selectedClipId)
            {
                ctx.state.selectClip (c.id);
                ctx.state.changed();
            }

            return getClip();
        }

    const auto& map = ctx.document.getTempoMap();
    const int bar = map.tickToBar (abs);
    const auto barEnd = map.barToTick (bar + 1);
    const auto trackId = track->id;

    // 選択中のクリップの後ろで、間にほかのクリップがなければ、そのクリップを伸ばす
    if (auto* sel = getClip(); sel != nullptr && abs >= sel->endTick()
                                && track->findMidiClip (sel->id) != nullptr)
    {
        const bool blocked = std::any_of (track->midiClips.begin(), track->midiClips.end(), [&] (auto& c)
        {
            return c.id != sel->id && c.startTick >= sel->endTick() && c.startTick < barEnd;
        });

        if (! blocked)
        {
            const auto clipId = sel->id;
            const auto newLength = barEnd - sel->startTick;
            ctx.document.perform ("クリップを伸ばす"_ju, [trackId, clipId, newLength] (collab::Project& p)
            {
                if (auto* t = p.findTrack (trackId))
                    if (auto* c = t->findMidiClip (clipId))
                        c->lengthTick = newLength;
            });
            return getClip();
        }
    }

    // その小節に新しいクリップ（次のクリップにかからない長さ）
    collab::MidiClip clip;
    clip.id = collab::generateUuid();
    clip.startTick = map.barToTick (bar);
    clip.lengthTick = barEnd - clip.startTick;

    for (auto& c : track->midiClips)
        if (c.startTick > clip.startTick && c.startTick < clip.endTick())
            clip.lengthTick = c.startTick - clip.startTick;

    if (clip.lengthTick <= 0)
        return nullptr;

    ctx.document.perform ("MIDI クリップを作る"_ju, [trackId, clip] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            t->midiClips.push_back (clip);
    });

    ctx.state.selectClip (clip.id);
    ctx.state.changed();
    return getClip();
}

void PianoRollView::editNotes (const juce::String& description, std::function<void (collab::MidiClip&)> fn, const juce::String& mergeId)
{
    const auto trackId = ctx.state.selectedTrackId, clipId = ctx.state.selectedClipId;

    ctx.document.perform (description, [trackId, clipId, fn] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            if (auto* c = t->findMidiClip (clipId))
                fn (*c);
    }, mergeId);
}

collab::Tick PianoRollView::snap (double absoluteTick, bool floor, const juce::ModifierKeys& mods) const
{
    const auto t = (collab::Tick) std::llround (juce::jmax (0.0, absoluteTick));

    if (mods.isAltDown())
        return t;

    const auto& map = ctx.document.getTempoMap();
    return floor ? ctx.state.grid.snapFloor (t, map) : ctx.state.grid.snap (t, map);
}

bool PianoRollView::deleteSelectedNotes()
{
    if (selectedNotes.empty() || getClip() == nullptr)
        return false;

    auto sel = selectedNotes;
    selectedNotes.clear();
    editNotes ("ノートの削除"_ju, [sel] (collab::MidiClip& c)
    {
        c.notes.erase (std::remove_if (c.notes.begin(), c.notes.end(), [&] (auto& n) { return sel.count (n.id) > 0; }), c.notes.end());
    });
    return true;
}

void PianoRollView::selectAllNotes()
{
    if (auto* clip = getClip())
    {
        selectedNotes.clear();

        for (auto& n : clip->notes)
            selectedNotes.insert (n.id);

        repaint();
    }
}

void PianoRollView::quantiseSelection()
{
    std::vector<std::string> ids (selectedNotes.begin(), selectedNotes.end());
    const auto g = ctx.state.grid;
    const auto map = ctx.document.getTempoMap();

    editNotes ("クオンタイズ"_ju, [ids, g, map] (collab::MidiClip& c)
    {
        collab::quantiseNotes (c, ids, g, map);
    });
}
