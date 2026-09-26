#include "ChordLane.h"

#include "ChordEditor.h"
#include "TimeGrid.h"
#include "collab/ChordPlayback.h"
#include "collab/Uuid.h"

namespace
{
    const juce::Colour chordColour { 0xffffb74d };

    void applyResult (collab::ChordEvent& e, const ChordEditor::Result& r)
    {
        e.noChord = r.noChord;
        e.chord = r.chord ? std::optional (collab::toChordSymbol (*r.chord)) : std::nullopt;
        e.text = toStd (r.text);
    }
}

ChordLane::ChordLane (AppContext& c) : ctx (c)
{
    setWantsKeyboardFocus (true);
    setTooltip ("コード: ダブルクリックで追加・編集、ドラッグで移動（1拍単位、Alt で自由）、Delete で削除"_ju);
    ctx.document.addChangeListener (this);
    ctx.state.addChangeListener (this);
}

ChordLane::~ChordLane()
{
    ctx.document.removeChangeListener (this);
    ctx.state.removeChangeListener (this);
}

juce::String ChordLane::displayText (const collab::ChordEvent& e)
{
    if (e.noChord)
        return "X";

    if (e.chord)
        return toJuce (collab::chord::format (collab::toChord (*e.chord)));

    return toJuce (e.text);
}

void ChordLane::paint (juce::Graphics& g)
{
    const auto& axis = ctx.state.timeline;
    const auto& map = ctx.document.getTempoMap();
    const auto& project = ctx.document.getProject();

    g.fillAll (Theme::laneAlt);
    TimeGrid::drawGrid (g, getLocalBounds(), axis, map, nullptr);

    auto events = project.chordTrack.events;
    std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });
    const auto end = collab::chordTrackEndTick (project, map);

    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));

    for (size_t i = 0; i < events.size(); ++i)
    {
        const auto& e = events[i];
        const float x1 = (float) axis.tickToX ((double) e.tick);
        const float x2 = (float) axis.tickToX ((double) (i + 1 < events.size() ? events[i + 1].tick : end));

        if (x2 < 0 || x1 > (float) getWidth())
            continue;

        const bool selected = e.id == ctx.state.selectedChordId;
        const auto r = juce::Rectangle<float> (x1, 2.0f, juce::jmax (4.0f, x2 - x1 - 1.0f), (float) getHeight() - 4.0f);
        const auto colour = e.noChord ? Theme::textDim : chordColour;

        g.setColour (colour.withAlpha (e.noChord ? 0.12f : 0.22f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (selected ? Theme::selection : colour);
        g.drawRoundedRectangle (r, 3.0f, selected ? 2.0f : 1.0f);
        g.setColour (e.noChord ? Theme::textDim : Theme::text);
        g.drawText (displayText (e), r.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

std::string ChordLane::findHit (float x) const
{
    const auto& axis = ctx.state.timeline;
    const auto tick = axis.xToTick (x);
    auto events = ctx.document.getProject().chordTrack.events;
    std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });

    // 先頭付近を優先（ドラッグしやすいように）、なければ区間内
    for (auto& e : events)
        if (std::abs ((float) axis.tickToX ((double) e.tick) - x) <= 6.0f)
            return e.id;

    std::string hit;
    for (auto& e : events)
        if ((double) e.tick <= tick)
            hit = e.id;

    return hit;
}

collab::Tick ChordLane::snapToBeat (double tick, const juce::ModifierKeys& mods) const
{
    const auto& map = ctx.document.getTempoMap();
    const auto t = (collab::Tick) std::llround (juce::jmax (0.0, tick));

    if (mods.isAltDown())
        return t;

    const auto sig = map.timeSignatureAtTick (t);
    return collab::Grid { sig.denominator, false, true }.snap (t, map);
}

void ChordLane::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragId = findHit (e.position.x);
    mergeId = juce::Uuid().toString();
    ctx.state.selectedChordId = dragId;
    ctx.state.changed();

    if (dragId.empty())
        return;

    for (auto& ev : ctx.document.getProject().chordTrack.events)
        if (ev.id == dragId)
            dragOrigTick = ev.tick;

    dragDownTick = ctx.state.timeline.xToTick (e.position.x);

    if (e.mods.isPopupMenu())
    {
        auto id = dragId;
        dragId = {};

        juce::PopupMenu m;
        m.addItem ("コードを編集…"_ju, [this, id] { openEditor (id); });
        m.addItem ("ノーコード（X）にする"_ju, [this, id]
        {
            ctx.document.perform ("コードの変更"_ju, [id] (collab::Project& p)
            {
                for (auto& x : p.chordTrack.events)
                    if (x.id == id)
                    {
                        x.noChord = true;
                        x.chord.reset();
                        x.text = "X";
                    }
            });
        });
        m.addItem ("削除"_ju, [this] { deleteSelected(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }
}

void ChordLane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragId.empty() || e.getDistanceFromDragStart() < 3)
        return;

    const auto tick = snapToBeat ((double) dragOrigTick + ctx.state.timeline.xToTick (e.position.x) - dragDownTick, e.mods);
    auto id = dragId;

    ctx.document.perform ("コードの移動"_ju, [id, tick] (collab::Project& p)
    {
        for (auto& x : p.chordTrack.events)
            if (x.id != id && x.tick == tick)
                return;   // 同じ位置に2つ置かない

        for (auto& x : p.chordTrack.events)
            if (x.id == id)
                x.tick = tick;
    }, mergeId);
}

void ChordLane::mouseUp (const juce::MouseEvent&)
{
    dragId = {};
    ctx.document.endMerge();
}

void ChordLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    // イベントの先頭近くをダブルクリックしたら編集、それ以外は新規作成
    const auto tick = snapToBeat (ctx.state.timeline.xToTick (e.position.x), e.mods);

    for (auto& ev : ctx.document.getProject().chordTrack.events)
    {
        if (ev.tick == tick || std::abs ((float) ctx.state.timeline.tickToX ((double) ev.tick) - e.position.x) <= 6.0f)
        {
            openEditor (ev.id);
            return;
        }
    }

    // 区間の中なら、その位置に新しいコードを置く
    addAt (tick);
}

bool ChordLane::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        return deleteSelected();

    if (key == juce::KeyPress::returnKey && ! ctx.state.selectedChordId.empty())
    {
        openEditor (ctx.state.selectedChordId);
        return true;
    }

    return false;
}

bool ChordLane::deleteSelected()
{
    const auto id = ctx.state.selectedChordId;

    if (id.empty())
        return false;

    ctx.document.perform ("コードの削除"_ju, [id] (collab::Project& p)
    {
        auto& ev = p.chordTrack.events;
        ev.erase (std::remove_if (ev.begin(), ev.end(), [&] (auto& x) { return x.id == id; }), ev.end());
    });

    ctx.state.selectedChordId = {};
    ctx.state.changed();
    return true;
}

void ChordLane::openEditor (const std::string& id)
{
    for (auto& ev : ctx.document.getProject().chordTrack.events)
    {
        if (ev.id != id)
            continue;

        auto* doc = &ctx.document;
        ChordEditor::show (displayText (ev),
                           [doc, id] (ChordEditor::Result r)
                           {
                               doc->perform ("コードの変更"_ju, [id, r] (collab::Project& p)
                               {
                                   for (auto& x : p.chordTrack.events)
                                       if (x.id == id)
                                           applyResult (x, r);
                               });
                           },
                           [this, id] { ctx.state.selectedChordId = id; deleteSelected(); });
        return;
    }
}

void ChordLane::addAt (collab::Tick tick)
{
    // 直前のコードを初期値にする（同じコードの続きを打ちやすく）
    juce::String initial = "C";
    collab::Tick best = -1;

    for (auto& ev : ctx.document.getProject().chordTrack.events)
        if (ev.tick <= tick && ev.tick > best)
        {
            best = ev.tick;
            initial = displayText (ev);
        }

    auto* doc = &ctx.document;
    auto* state = &ctx.state;

    ChordEditor::show (initial, [doc, state, tick] (ChordEditor::Result r)
    {
        collab::ChordEvent e;
        e.id = collab::generateUuid();
        e.tick = tick;
        applyResult (e, r);

        doc->perform ("コードの追加"_ju, [e] (collab::Project& p)
        {
            p.chordTrack.events.push_back (e);
        });

        state->selectedChordId = e.id;
        state->changed();
    });
}
