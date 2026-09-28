#include "KeyLane.h"

#include "Dialogs.h"
#include "TimeGrid.h"
#include "collab/ChordPlayback.h"
#include "collab/Uuid.h"

namespace
{
    const juce::Colour keyColour { 0xff9ccc65 };
    constexpr float hitRadius = 6.0f;

    juce::String longName (const collab::chord::Key& k)
    {
        return toJuce (collab::chord::keyName (k)) + (k.minor ? "（マイナー）"_ju : "（メジャー）"_ju);
    }
}

KeyLane::KeyLane (AppContext& c) : ctx (c)
{
    setWantsKeyboardFocus (true);
    setTooltip ({});
    ctx.document.addChangeListener (this);
    ctx.state.addChangeListener (this);
}

KeyLane::~KeyLane()
{
    ctx.document.removeChangeListener (this);
    ctx.state.removeChangeListener (this);
}

juce::PopupMenu KeyLane::keyMenu (std::optional<collab::chord::Key> current, std::function<void (collab::chord::Key)> onPick)
{
    juce::PopupMenu m;

    // アルファベット順（A, Ab, B, Bb, C, C#, D …。同じ文字なら ♮ → ♭ / ♯ の順）で探しやすく
    for (bool minor : { false, true })
    {
        m.addSectionHeader (minor ? "マイナー"_ju : "メジャー"_ju);

        std::vector<collab::chord::Key> keys;

        for (int i = 0; i < 12; ++i)
            keys.push_back ({ i, minor });

        std::sort (keys.begin(), keys.end(), [] (const auto& a, const auto& b)
        {
            const auto na = collab::chord::keyName (a), nb = collab::chord::keyName (b);
            return na[0] != nb[0] ? na[0] < nb[0] : na.size() < nb.size();
        });

        for (auto& k : keys)
            m.addItem (longName (k), true, current && *current == k, [onPick, k] { onPick (k); });

        if (! minor)
            m.addColumnBreak();
    }

    return m;
}

void KeyLane::paint (juce::Graphics& g)
{
    const auto& axis = ctx.state.timeline;
    const auto& map = ctx.document.getTempoMap();
    const auto& events = ctx.document.getProject().keyTrack.events;

    g.fillAll (Theme::laneAlt);
    TimeGrid::drawGrid (g, getLocalBounds(), axis, map, nullptr);

    if (events.empty())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (14.5f));
        g.drawText ("キー未設定（右クリックで設定。コードから推定もできます）"_ju, getLocalBounds().reduced (6, 0),
                    juce::Justification::centredLeft);
        return;
    }

    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    // 左にスクロールして変更点が見えなくなっても、いまのキーを左端に出す
    {
        const collab::KeyEvent* current = nullptr;
        float nextX = (float) getWidth();

        for (auto& e : events)
        {
            const float x = (float) axis.tickToX ((double) map.barToTick (e.bar));

            if (x < 0.0f && (current == nullptr || e.bar > current->bar))
                current = &e;
            else if (x >= 0.0f)
                nextX = juce::jmin (nextX, x);
        }

        if (current != nullptr && nextX > 90.0f)
        {
            g.setColour (keyColour.withAlpha (0.2f));
            g.fillRect (juce::Rectangle<float> (0.0f, 3.0f, 80.0f, (float) getHeight() - 6.0f));
            g.setColour (Theme::text);
            g.drawText ("Key: "_ju + toJuce (collab::chord::keyName ({ current->tonic, current->minor })),
                        juce::Rectangle<float> (5.0f, 0.0f, 110.0f, (float) getHeight()), juce::Justification::centredLeft);
        }
    }

    for (auto& e : events)
    {
        const float x = (float) axis.tickToX ((double) map.barToTick (e.bar));

        if (x < -120.0f || x > (float) getWidth())
            continue;

        const bool selected = e.id == ctx.state.selectedKeyId;
        g.setColour (selected ? Theme::selection : keyColour);
        g.fillRect (juce::Rectangle<float> (x, 0.0f, selected ? 3.0f : 2.0f, (float) getHeight()));
        g.setColour (Theme::text);
        g.drawText ("Key: "_ju + toJuce (collab::chord::keyName ({ e.tonic, e.minor })),
                    juce::Rectangle<float> (x + 5.0f, 0.0f, 110.0f, (float) getHeight()), juce::Justification::centredLeft);
    }
}

std::string KeyLane::findHit (float x) const
{
    const auto& map = ctx.document.getTempoMap();

    for (auto& e : ctx.document.getProject().keyTrack.events)
        if (std::abs ((float) ctx.state.timeline.tickToX ((double) map.barToTick (e.bar)) - x) <= hitRadius)
            return e.id;

    return {};
}

int KeyLane::barAt (float x) const
{
    return ctx.document.getTempoMap().tickToBar ((collab::Tick) juce::jmax (0.0, ctx.state.timeline.xToTick (x)));
}

void KeyLane::mouseMove (const juce::MouseEvent& e)
{
    setGhost (ctx.state.pencil() && findHit (e.position.x).empty() ? (double) ctx.document.getTempoMap().barToTick (barAt (e.position.x)) : -1.0);
    setMouseCursor (ctx.state.pencil() ? Theme::pencilCursor()
                                       : ! findHit (e.position.x).empty() ? juce::MouseCursor::LeftRightResizeCursor
                                                                          : juce::MouseCursor::NormalCursor);
}

void KeyLane::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragId = findHit (e.position.x);
    mergeId = juce::Uuid().toString();
    const int bar = barAt (e.position.x);

    if (e.mods.isPopupMenu())
    {
        auto id = dragId;
        dragId = {};
        return showMenu (id, bar);
    }

    ctx.state.selectedKeyId = dragId;
    ctx.state.changed();

    // 鉛筆ツールで空いている所をクリックしたら、その小節にキーを置く（キーを選ぶメニュー）
    if (dragId.empty() && ctx.state.pencil())
    {
        const auto current = collab::keyAt (ctx.document.getProject(), ctx.document.getTempoMap(), ctx.document.getTempoMap().barToTick (bar));
        keyMenu (current, [this, bar] (collab::chord::Key k) { setKeyAt (bar, k); })
            .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }
}

void KeyLane::showMenu (const std::string& id, int bar)
{
    juce::PopupMenu m;
    const auto& map = ctx.document.getTempoMap();

    if (! id.empty())
    {
        ctx.state.selectedKeyId = id;
        ctx.state.changed();

        std::optional<collab::chord::Key> current;

        for (auto& e : ctx.document.getProject().keyTrack.events)
            if (e.id == id)
                current = collab::chord::Key { e.tonic, e.minor };

        m.addSubMenu ("キーを変更"_ju, keyMenu (current, [this, id] (collab::chord::Key k)
        {
            ctx.document.perform ("キーの変更"_ju, [id, k] (collab::Project& p)
            {
                for (auto& x : p.keyTrack.events)
                    if (x.id == id)
                        std::tie (x.tonic, x.minor) = std::tie (k.tonic, k.minor);
            });
        }));

        m.addItem ("削除"_ju, [this] { deleteSelected(); });
    }
    else
    {
        const auto current = collab::keyAt (ctx.document.getProject(), map, map.barToTick (bar));
        m.addSubMenu (juce::String (bar) + " 小節目にキーを設定"_ju, keyMenu (current, [this, bar] (collab::chord::Key k) { setKeyAt (bar, k); }));
    }

    m.addSeparator();
    m.addItem ("コード進行からキーを推定して設定"_ju, ! ctx.document.getProject().chordTrack.events.empty(), false,
               [this, bar = id.empty() && ! ctx.document.getProject().keyTrack.events.empty() ? bar : 1] { estimateFromChords (bar); });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void KeyLane::setKeyAt (int bar, collab::chord::Key k)
{
    setKey (ctx, bar, k);
}

void KeyLane::setKey (AppContext& ctx, int bar, collab::chord::Key k)
{
    // 同じ小節にあれば書き換え、なければ追加
    std::string id = collab::generateUuid();

    for (auto& x : ctx.document.getProject().keyTrack.events)
        if (x.bar == bar)
            id = x.id;

    ctx.document.perform ("キーの設定"_ju, [bar, k, id] (collab::Project& p)
    {
        if (p.keyTrack.id.empty())
            p.keyTrack.id = collab::keyTrackIdFor (p.projectId);

        for (auto& x : p.keyTrack.events)
            if (x.id == id || x.bar == bar)
            {
                x.tonic = k.tonic;
                x.minor = k.minor;
                return;
            }

        p.keyTrack.events.push_back ({ id, bar, k.tonic, k.minor });
    });

    ctx.state.selectedKeyId = id;
    ctx.state.changed();
}

void KeyLane::estimateFromChords (int bar)
{
    const auto& map = ctx.document.getTempoMap();
    const auto from = map.barToTick (bar);
    std::vector<collab::chord::Chord> chords;

    auto events = ctx.document.getProject().chordTrack.events;
    std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });

    for (auto& e : events)
        if (e.tick >= from && e.chord && ! e.noChord)
            chords.push_back (collab::toChord (*e.chord));

    auto k = collab::chord::estimateKey (chords);

    if (! k)
        return Dialogs::showInfo ("キーの推定"_ju, "推定に使えるコードがありません。"_ju);

    setKeyAt (bar, *k);
}

void KeyLane::changeKey (const std::string& id)
{
    for (auto& e : ctx.document.getProject().keyTrack.events)
    {
        if (e.id != id)
            continue;

        keyMenu (collab::chord::Key { e.tonic, e.minor }, [this, id] (collab::chord::Key k)
        {
            ctx.document.perform ("キーの変更"_ju, [id, k] (collab::Project& p)
            {
                for (auto& x : p.keyTrack.events)
                    if (x.id == id)
                        std::tie (x.tonic, x.minor) = std::tie (k.tonic, k.minor);
            });
        }).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }
}

bool KeyLane::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        return deleteSelected();

    if (key == juce::KeyPress::returnKey && ! ctx.state.selectedKeyId.empty())
    {
        changeKey (ctx.state.selectedKeyId);
        return true;
    }

    return false;
}

bool KeyLane::deleteSelected()
{
    const auto id = ctx.state.selectedKeyId;

    if (id.empty())
        return false;

    ctx.document.perform ("キーの削除"_ju, [id] (collab::Project& p)
    {
        auto& ev = p.keyTrack.events;
        ev.erase (std::remove_if (ev.begin(), ev.end(), [&] (auto& x) { return x.id == id; }), ev.end());
    });

    ctx.state.selectedKeyId = {};
    ctx.state.changed();
    return true;
}

void KeyLane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragId.empty() || e.getDistanceFromDragStart() < 3)
        return;

    const auto& map = ctx.document.getTempoMap();
    const auto tick = (collab::Tick) juce::jmax (0.0, ctx.state.timeline.xToTick (e.position.x));
    int bar = map.tickToBar (tick);

    // 近い方の小節の頭へ
    if (tick - map.barToTick (bar) > map.barToTick (bar + 1) - tick)
        ++bar;

    bar = juce::jmax (1, bar);
    auto id = dragId;

    ctx.document.perform ("キーの移動"_ju, [id, bar] (collab::Project& p)
    {
        for (auto& x : p.keyTrack.events)
            if (x.id != id && x.bar == bar)
                return;   // 同じ小節に 2 つ置かない

        for (auto& x : p.keyTrack.events)
            if (x.id == id)
                x.bar = bar;
    }, mergeId);
}

void KeyLane::mouseUp (const juce::MouseEvent&)
{
    dragId = {};
    ctx.document.endMerge();
}

void KeyLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (auto id = findHit (e.position.x); ! id.empty())
        changeKey (id);
}

void KeyLane::setGhost (double tick)
{
    if (std::abs (tick - ghostTick) > 0.5)
    {
        ghostTick = tick;
        repaint();
    }
}

void KeyLane::mouseExit (const juce::MouseEvent&)
{
    setGhost (-1.0);
}

void KeyLane::paintOverChildren (juce::Graphics& g)
{
    // 鉛筆ツール: クリックしたら置かれる位置
    if (ghostTick >= 0.0 && ctx.state.pencil())
        TimeGrid::drawPencilGhostLine (g, (float) ctx.state.timeline.tickToX (ghostTick), getHeight());
}
