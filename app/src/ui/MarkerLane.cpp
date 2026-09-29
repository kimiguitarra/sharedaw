#include "MarkerLane.h"

#include "Dialogs.h"
#include "TimeGrid.h"
#include "collab/Uuid.h"

namespace
{
    const juce::Colour markerColour { 0xff4dd0e1 };

    juce::String labelText (const collab::Marker& m, int number)
    {
        return juce::String (number) + (m.name.empty() ? juce::String() : "  " + toJuce (m.name));
    }
}

MarkerLane::MarkerLane (AppContext& c) : ctx (c)
{
    setWantsKeyboardFocus (true);
    setTooltip ({});
    ctx.document.addChangeListener (this);
    ctx.state.addChangeListener (this);
}

MarkerLane::~MarkerLane()
{
    ctx.document.removeChangeListener (this);
    ctx.state.removeChangeListener (this);
}

std::vector<collab::Marker> MarkerLane::sorted (const collab::Project& p)
{
    auto events = p.markerTrack.events;
    std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return std::tie (a.tick, a.id) < std::tie (b.tick, b.id); });
    return events;
}

void MarkerLane::addMarker (AppContext& ctx, collab::Tick tick)
{
    for (auto& m : ctx.document.getProject().markerTrack.events)
        if (m.tick == tick)
            return;

    collab::Marker m;
    m.id = collab::generateUuid();
    m.tick = tick;

    ctx.document.perform ("マーカーの追加"_ju, [m] (collab::Project& p) { p.markerTrack.events.push_back (m); });
    ctx.state.selectedMarkerId = m.id;
    ctx.state.changed();
}

juce::Rectangle<float> MarkerLane::labelBounds (const collab::Marker& m, int number) const
{
    const float x = (float) timeAxis().tickToX ((double) m.tick);
    const auto font = juce::Font (juce::FontOptions (15.0f, juce::Font::bold));
    const float w = juce::GlyphArrangement::getStringWidth (font, labelText (m, number)) + 12.0f;
    return { x, 2.0f, juce::jmax (18.0f, w), (float) getHeight() - 4.0f };
}

void MarkerLane::paint (juce::Graphics& g)
{
    const auto& axis = timeAxis();
    g.fillAll (Theme::laneAlt);
    TimeGrid::drawGrid (g, getLocalBounds(), axis, ctx.document.getTempoMap(), nullptr);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    const auto markers = sorted (ctx.document.getProject());

    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto& m = markers[i];
        const auto r = labelBounds (m, (int) i + 1);

        if (r.getRight() < 0 || r.getX() > (float) getWidth())
            continue;

        const bool selected = m.id == ctx.state.selectedMarkerId || ctx.state.rangeMarkerIds.count (m.id) > 0;
        g.setColour (markerColour.withAlpha (0.25f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (selected ? Theme::selection : markerColour);
        g.fillRect (r.getX(), 0.0f, 2.0f, (float) getHeight());
        g.drawRoundedRectangle (r, 3.0f, selected ? 2.0f : 1.0f);
        g.setColour (Theme::text);
        g.drawText (labelText (m, (int) i + 1), r.withTrimmedLeft (6.0f), juce::Justification::centredLeft, false);
    }
}

std::string MarkerLane::findHit (float x) const
{
    const auto markers = sorted (ctx.document.getProject());

    // 後ろ（右）のマーカーを優先（ラベルが重なったとき）
    for (int i = (int) markers.size() - 1; i >= 0; --i)
    {
        const auto r = labelBounds (markers[(size_t) i], i + 1);

        if (x >= r.getX() - 4.0f && x <= r.getRight())
            return markers[(size_t) i].id;
    }

    return {};
}

collab::Tick MarkerLane::snap (double tick, const juce::ModifierKeys& mods) const
{
    return (collab::Tick) std::llround (ctx.state.snapCursor (tick, ctx.document.getTempoMap(), mods));
}

void MarkerLane::mouseMove (const juce::MouseEvent& e)
{
    setGhost (ctx.state.pencil() && findHit (e.position.x).empty() ? (double) snap (timeAxis().xToTick (e.position.x), e.mods) : -1.0);
    // マーカーの上は矢印、何もない所は I 字（クリックで再生位置）
    const bool empty = findHit (e.position.x).empty();
    setMouseCursor (! empty ? juce::MouseCursor::NormalCursor
                            : ctx.state.pencil() ? Theme::pencilCursor() : juce::MouseCursor::IBeamCursor);
}

void MarkerLane::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    mergeId = juce::Uuid().toString();
    const auto tick = snap (timeAxis().xToTick (e.position.x), e.mods);
    dragId = findHit (e.position.x);

    if (dragId.empty())
    {
        ctx.state.selectedMarkerId = {};
        ctx.state.changed();

        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem ("ここにマーカーを追加"_ju, [this, tick] { addMarker (ctx, tick); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }
        else if (ctx.state.pencil())
        {
            addMarker (ctx, tick);
        }

        return;
    }

    ctx.state.selectedMarkerId = dragId;
    ctx.state.changed();

    for (auto& m : ctx.document.getProject().markerTrack.events)
        if (m.id == dragId)
            dragOrigTick = m.tick;

    dragDownTick = timeAxis().xToTick (e.position.x);

    if (e.mods.isPopupMenu())
    {
        auto id = dragId;
        const auto at = dragOrigTick;
        dragId = {};

        juce::PopupMenu m;
        m.addItem ("ここへ移動（再生位置）"_ju, [this, at] { ctx.engine.setPositionTick ((double) at); });
        m.addItem ("名前を変更…"_ju, [this, id] { rename (id); });
        m.addItem ("削除"_ju, [this] { deleteSelected(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }
}

void MarkerLane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragId.empty() || e.getDistanceFromDragStart() < 3)
        return;

    const auto tick = juce::jmax<collab::Tick> (0, snap ((double) dragOrigTick + timeAxis().xToTick (e.position.x) - dragDownTick, e.mods));
    auto id = dragId;

    ctx.document.perform ("マーカーの移動"_ju, [id, tick] (collab::Project& p)
    {
        for (auto& x : p.markerTrack.events)
            if (x.id != id && x.tick == tick)
                return;   // 同じ位置に 2 つ置かない

        for (auto& x : p.markerTrack.events)
            if (x.id == id)
                x.tick = tick;
    }, mergeId);
}

void MarkerLane::mouseUp (const juce::MouseEvent&)
{
    dragId = {};
    ctx.document.endMerge();
}

void MarkerLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (ctx.state.pencil())
        return;

    if (auto id = findHit (e.position.x); ! id.empty())
        rename (id);
}

bool MarkerLane::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        return deleteSelected();

    if (key == juce::KeyPress::returnKey && ! ctx.state.selectedMarkerId.empty())
    {
        rename (ctx.state.selectedMarkerId);
        return true;
    }

    return false;
}

bool MarkerLane::deleteSelected()
{
    const auto id = ctx.state.selectedMarkerId;

    if (id.empty())
        return false;

    ctx.document.perform ("マーカーの削除"_ju, [id] (collab::Project& p)
    {
        auto& ev = p.markerTrack.events;
        ev.erase (std::remove_if (ev.begin(), ev.end(), [&] (auto& x) { return x.id == id; }), ev.end());
    });

    ctx.state.selectedMarkerId = {};
    ctx.state.changed();
    return true;
}

void MarkerLane::rename (const std::string& id)
{
    for (auto& m : ctx.document.getProject().markerTrack.events)
    {
        if (m.id != id)
            continue;

        auto* doc = &ctx.document;
        Dialogs::askText ("マーカーの名前"_ju, "名前（例: Aメロ、サビ）。空にすると番号だけになります"_ju, toJuce (m.name),
                          [doc, id] (const juce::String& text)
        {
            const auto name = toStd (text.trim());
            doc->perform ("マーカーの名前"_ju, [id, name] (collab::Project& p)
            {
                for (auto& x : p.markerTrack.events)
                    if (x.id == id)
                        x.name = name;
            });
        }, this);
        return;
    }
}

void MarkerLane::setGhost (double tick)
{
    if (std::abs (tick - ghostTick) > 0.5)
    {
        ghostTick = tick;
        repaint();
    }
}

void MarkerLane::mouseExit (const juce::MouseEvent&)
{
    setGhost (-1.0);
}

void MarkerLane::paintOverChildren (juce::Graphics& g)
{
    // 鉛筆ツール: クリックしたら置かれる位置
    if (ghostTick >= 0.0 && ctx.state.pencil())
        TimeGrid::drawPencilGhostLine (g, (float) timeAxis().tickToX (ghostTick), getHeight());
}
