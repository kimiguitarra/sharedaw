#include "TempoMeterLanes.h"

#include "Dialogs.h"
#include "TimeGrid.h"
#include "collab/Uuid.h"

namespace
{
    constexpr float hitRadius = 6.0f;

    juce::String formatBpm (double bpm)
    {
        return juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.005 ? 0 : 2);
    }

    bool projectHasAudio (const collab::Project& p)
    {
        for (auto& t : p.tracks)
            if (! t.audioClips.empty())
                return true;

        return false;
    }

    juce::String audioWarning (const collab::Project& p)
    {
        return projectHasAudio (p) ? "\n\n※ オーディオはテンポに追従しません。テンポを変えるとオーディオとの位置がずれます。"_ju
                                   : juce::String();
    }
}

//==============================================================================
TempoLane::TempoLane (ProjectDocument& d, EditorState& s) : document (d), state (s)
{
    document.addChangeListener (this);
    state.addChangeListener (this);
    setWantsKeyboardFocus (true);
    setTooltip ("テンポ: 鉛筆ツールでクリックして追加。選択ツールでドラッグして移動、ダブルクリックで編集、Delete で削除"_ju);
}

TempoLane::~TempoLane()
{
    document.removeChangeListener (this);
    state.removeChangeListener (this);
}

void TempoLane::paint (juce::Graphics& g)
{
    const auto& axis = state.timeline;
    g.fillAll (Theme::laneAlt);
    TimeGrid::drawGrid (g, getLocalBounds(), axis, document.getTempoMap(), nullptr);

    const auto& events = document.getProject().tempoTrack.events;
    g.setFont (juce::FontOptions (12.0f));

    for (size_t i = 0; i < events.size(); ++i)
    {
        const auto& e = events[i];
        const float x = (float) axis.tickToX ((double) e.tick);
        const float next = i + 1 < events.size() ? (float) axis.tickToX ((double) events[i + 1].tick) : (float) getWidth();

        if (next < 0 || x > (float) getWidth())
            continue;

        g.setColour (Theme::tempo.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> (x, 2.0f, next - x, (float) getHeight() - 4.0f));
        const bool selected = e.id == state.selectedTempoId;
        g.setColour (selected ? Theme::selection : Theme::tempo);
        g.fillRect (juce::Rectangle<float> (x, 0.0f, selected ? 3.0f : 2.0f, (float) getHeight()));
        // 左にスクロールして変更点が見えなくなっても、値は左端に残す（次の変更点の手前まで）
        const float textX = juce::jmin (juce::jmax (x + 4.0f, 4.0f), juce::jmax (x + 4.0f, next - 64.0f));
        g.setColour (Theme::text);
        g.drawText (formatBpm (e.bpm), juce::Rectangle<float> (textX, 0.0f, 80.0f, (float) getHeight()),
                    juce::Justification::centredLeft);
    }
}

std::string TempoLane::findHit (float x) const
{
    for (auto& e : document.getProject().tempoTrack.events)
        if (std::abs ((float) state.timeline.tickToX ((double) e.tick) - x) <= hitRadius)
            return e.id;

    return {};
}

void TempoLane::mouseMove (const juce::MouseEvent& e)
{
    setGhost (state.pencil() && findHit (e.position.x).empty()
                ? (double) state.timelineGrid.snap ((collab::Tick) juce::jmax (0.0, state.timeline.xToTick (e.position.x)), document.getTempoMap())
                : -1.0);
    setMouseCursor (state.pencil() ? Theme::pencilCursor()
                                   : ! findHit (e.position.x).empty() ? juce::MouseCursor::LeftRightResizeCursor
                                                                      : juce::MouseCursor::NormalCursor);
}

void TempoLane::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragId = findHit (e.position.x);
    mergeId = juce::Uuid().toString();
    const auto tick = state.timelineGrid.snap ((collab::Tick) juce::jmax (0.0, state.timeline.xToTick (e.position.x)),
                                               document.getTempoMap());

    if (e.mods.isPopupMenu())
    {
        auto id = dragId;
        dragId = {};
        return showMenu (id, tick);
    }

    state.selectedTempoId = dragId;
    state.changed();

    // 鉛筆ツールで空いている所をクリックしたら追加
    if (dragId.empty() && state.pencil())
        addEventAt (tick);
}

void TempoLane::showMenu (const std::string& id, collab::Tick tick)
{
    juce::PopupMenu m;

    if (! id.empty())
    {
        const auto& events = document.getProject().tempoTrack.events;
        const bool isFirst = ! events.empty() && events.front().id == id;
        state.selectedTempoId = id;
        state.changed();

        m.addItem ("テンポを編集…"_ju, [this, id] { editEvent (id); });
        m.addItem ("削除"_ju, ! isFirst, false, [this] { deleteSelected(); });
    }
    else
    {
        m.addItem ("ここにテンポ変更を追加…"_ju, [this, tick] { addEventAt (tick); });
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

bool TempoLane::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        return deleteSelected();

    if (key == juce::KeyPress::returnKey && ! state.selectedTempoId.empty())
    {
        editEvent (state.selectedTempoId);
        return true;
    }

    return false;
}

bool TempoLane::deleteSelected()
{
    const auto id = state.selectedTempoId;
    const auto& events = document.getProject().tempoTrack.events;

    if (id.empty() || events.empty() || events.front().id == id)
        return false;   // 先頭（曲の最初のテンポ）は消せない

    document.perform ("テンポ変更の削除"_ju, [id] (collab::Project& p)
    {
        auto& ev = p.tempoTrack.events;
        ev.erase (std::remove_if (ev.begin(), ev.end(), [&] (auto& x) { return x.id == id; }), ev.end());
    });

    state.selectedTempoId = {};
    state.changed();
    return true;
}

void TempoLane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragId.empty() || e.getDistanceFromDragStart() < 3)
        return;

    const auto& events = document.getProject().tempoTrack.events;

    if (! events.empty() && events.front().id == dragId)
        return;   // 先頭（tick 0）のテンポは動かさない

    const auto tick = state.timelineGrid.snap ((collab::Tick) juce::jmax (1.0, state.timeline.xToTick (e.position.x)),
                                               document.getTempoMap());
    auto id = dragId;
    document.perform ("テンポ変更の移動"_ju, [id, tick] (collab::Project& p)
    {
        for (auto& ev : p.tempoTrack.events)
            if (ev.id == id)
                ev.tick = juce::jmax<collab::Tick> (1, tick);
    }, mergeId);
}

void TempoLane::mouseUp (const juce::MouseEvent&)
{
    dragId = {};
    document.endMerge();
}

void TempoLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (auto id = findHit (e.position.x); ! id.empty())
        editEvent (id);
}

void TempoLane::editEvent (const std::string& id)
{
    for (auto& ev : document.getProject().tempoTrack.events)
    {
        if (ev.id != id)
            continue;

        Dialogs::askText ("テンポの変更"_ju, "BPM（10〜999）"_ju + audioWarning (document.getProject()), formatBpm (ev.bpm),
                          [this, id] (const juce::String& text)
        {
            const double bpm = text.getDoubleValue();

            if (bpm < 10.0 || bpm > 999.0)
                return Dialogs::showError ("テンポ"_ju, "10〜999 の数値を入力してください。"_ju);

            document.perform ("テンポの変更"_ju, [id, bpm] (collab::Project& p)
            {
                for (auto& x : p.tempoTrack.events)
                    if (x.id == id)
                        x.bpm = bpm;
            });
        }, this);
        return;
    }
}

void TempoLane::addEventAt (collab::Tick tick)
{
    const double current = document.getTempoMap().bpmAtTick (tick);

    Dialogs::askText ("テンポ変更の追加"_ju, "BPM（10〜999）"_ju + audioWarning (document.getProject()), formatBpm (current),
                      [this, tick] (const juce::String& text)
    {
        const double bpm = text.getDoubleValue();

        if (bpm < 10.0 || bpm > 999.0)
            return Dialogs::showError ("テンポ"_ju, "10〜999 の数値を入力してください。"_ju);

        document.perform ("テンポ変更の追加"_ju, [tick, bpm] (collab::Project& p)
        {
            for (auto& x : p.tempoTrack.events)
            {
                if (x.tick == tick)
                {
                    x.bpm = bpm;
                    return;
                }
            }

            p.tempoTrack.events.push_back ({ collab::generateUuid(), tick, bpm });
        });
    }, this);
}

//==============================================================================
MeterLane::MeterLane (ProjectDocument& d, EditorState& s) : document (d), state (s)
{
    document.addChangeListener (this);
    state.addChangeListener (this);
    setWantsKeyboardFocus (true);
    setTooltip ("拍子: 鉛筆ツールでクリックして追加。選択ツールでドラッグして移動、ダブルクリックで編集、Delete で削除"_ju);
}

MeterLane::~MeterLane()
{
    document.removeChangeListener (this);
    state.removeChangeListener (this);
}

std::optional<std::pair<int, int>> MeterLane::parseMeter (const juce::String& text)
{
    auto parts = juce::StringArray::fromTokens (text.retainCharacters ("0123456789/"), "/", {});

    if (parts.size() != 2)
        return std::nullopt;

    const int num = parts[0].getIntValue(), den = parts[1].getIntValue();

    if (num < 1 || num > 64 || ! (den == 1 || den == 2 || den == 4 || den == 8 || den == 16 || den == 32))
        return std::nullopt;

    return std::make_pair (num, den);
}

void MeterLane::paint (juce::Graphics& g)
{
    const auto& axis = state.timeline;
    const auto& map = document.getTempoMap();
    g.fillAll (Theme::laneAlt);
    TimeGrid::drawGrid (g, getLocalBounds(), axis, map, nullptr);
    g.setFont (juce::FontOptions (12.0f));

    // 左にスクロールして変更点が見えなくなっても、いまの拍子を左端に出す
    {
        const collab::MeterEvent* current = nullptr;
        float nextX = (float) getWidth();

        for (auto& e : document.getProject().meterTrack.events)
        {
            const float x = (float) axis.tickToX ((double) map.barToTick (e.bar));

            if (x < 0.0f && (current == nullptr || e.bar > current->bar))
                current = &e;
            else if (x >= 0.0f)
                nextX = juce::jmin (nextX, x);
        }

        if (current != nullptr && nextX > 50.0f)
        {
            g.setColour (Theme::meter.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (0.0f, 3.0f, 44.0f, (float) getHeight() - 6.0f));
            g.setColour (Theme::text);
            g.drawText (juce::String (current->numerator) + "/" + juce::String (current->denominator),
                        juce::Rectangle<float> (4.0f, 0.0f, 60.0f, (float) getHeight()), juce::Justification::centredLeft);
        }
    }

    for (auto& e : document.getProject().meterTrack.events)
    {
        const float x = (float) axis.tickToX ((double) map.barToTick (e.bar));

        if (x < -60.0f || x > (float) getWidth())
            continue;

        const bool selected = e.id == state.selectedMeterId;
        g.setColour (selected ? Theme::selection : Theme::meter);
        g.fillRect (juce::Rectangle<float> (x, 0.0f, selected ? 3.0f : 2.0f, (float) getHeight()));
        g.setColour (Theme::text);
        g.drawText (juce::String (e.numerator) + "/" + juce::String (e.denominator),
                    juce::Rectangle<float> (x + 4.0f, 0.0f, 60.0f, (float) getHeight()), juce::Justification::centredLeft);
    }
}

std::string MeterLane::findHit (float x) const
{
    const auto& map = document.getTempoMap();

    for (auto& e : document.getProject().meterTrack.events)
        if (std::abs ((float) state.timeline.tickToX ((double) map.barToTick (e.bar)) - x) <= hitRadius)
            return e.id;

    return {};
}

void MeterLane::mouseMove (const juce::MouseEvent& e)
{
    setGhost (state.pencil() && findHit (e.position.x).empty() ? (double) document.getTempoMap().barToTick (barAt (e.position.x)) : -1.0);
    setMouseCursor (state.pencil() ? Theme::pencilCursor()
                                   : ! findHit (e.position.x).empty() ? juce::MouseCursor::LeftRightResizeCursor
                                                                      : juce::MouseCursor::NormalCursor);
}

int MeterLane::barAt (float x) const
{
    return document.getTempoMap().tickToBar ((collab::Tick) juce::jmax (0.0, state.timeline.xToTick (x)));
}

void MeterLane::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragId = findHit (e.position.x);
    mergeId = juce::Uuid().toString();

    if (e.mods.isPopupMenu())
    {
        auto id = dragId;
        dragId = {};
        return showMenu (id, barAt (e.position.x));
    }

    state.selectedMeterId = dragId;
    state.changed();

    // 鉛筆ツールで空いている所をクリックしたら、その小節に追加
    if (dragId.empty() && state.pencil())
        addEventAt (barAt (e.position.x));
}

void MeterLane::showMenu (const std::string& id, int bar)
{
    juce::PopupMenu m;

    if (! id.empty())
    {
        const auto& events = document.getProject().meterTrack.events;
        const bool isFirst = ! events.empty() && events.front().id == id;
        state.selectedMeterId = id;
        state.changed();

        m.addItem ("拍子を編集…"_ju, [this, id] { editEvent (id); });
        m.addItem ("削除"_ju, ! isFirst, false, [this] { deleteSelected(); });
    }
    else
    {
        m.addItem (juce::String (bar) + " 小節目に拍子変更を追加…"_ju, [this, bar] { addEventAt (bar); });
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

bool MeterLane::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        return deleteSelected();

    if (key == juce::KeyPress::returnKey && ! state.selectedMeterId.empty())
    {
        editEvent (state.selectedMeterId);
        return true;
    }

    return false;
}

bool MeterLane::deleteSelected()
{
    const auto id = state.selectedMeterId;
    const auto& events = document.getProject().meterTrack.events;

    if (id.empty() || events.empty() || events.front().id == id)
        return false;   // 1 小節目の拍子は消せない

    document.perform ("拍子変更の削除"_ju, [id] (collab::Project& p)
    {
        auto& ev = p.meterTrack.events;
        ev.erase (std::remove_if (ev.begin(), ev.end(), [&] (auto& x) { return x.id == id; }), ev.end());
    });

    state.selectedMeterId = {};
    state.changed();
    return true;
}

void MeterLane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragId.empty() || e.getDistanceFromDragStart() < 3)
        return;

    const auto& events = document.getProject().meterTrack.events;

    if (! events.empty() && events.front().id == dragId)
        return;   // 1小節目の拍子は動かさない

    const auto& map = document.getTempoMap();
    const auto tick = (collab::Tick) juce::jmax (0.0, state.timeline.xToTick (e.position.x));
    int bar = map.tickToBar (tick);

    // 近い方の小節の頭へ
    if (tick - map.barToTick (bar) > map.barToTick (bar + 1) - tick)
        ++bar;

    bar = juce::jmax (2, bar);
    auto id = dragId;

    document.perform ("拍子変更の移動"_ju, [id, bar] (collab::Project& p)
    {
        for (auto& x : p.meterTrack.events)
            if (x.id != id && x.bar == bar)
                return;   // 同じ小節に2つ置かない

        for (auto& x : p.meterTrack.events)
            if (x.id == id)
                x.bar = bar;
    }, mergeId);
}

void MeterLane::mouseUp (const juce::MouseEvent&)
{
    dragId = {};
    document.endMerge();
}

void MeterLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (auto id = findHit (e.position.x); ! id.empty())
        editEvent (id);
}

void MeterLane::editEvent (const std::string& id)
{
    for (auto& ev : document.getProject().meterTrack.events)
    {
        if (ev.id != id)
            continue;

        Dialogs::askText ("拍子の変更"_ju, "拍子（例: 4/4, 3/4, 6/8, 5/4, 7/8）"_ju,
                          juce::String (ev.numerator) + "/" + juce::String (ev.denominator),
                          [this, id] (const juce::String& text)
        {
            auto m = parseMeter (text);

            if (! m)
                return Dialogs::showError ("拍子"_ju, "「4/4」のように入力してください（分母は 1, 2, 4, 8, 16, 32）。"_ju);

            document.perform ("拍子の変更"_ju, [id, m] (collab::Project& p)
            {
                for (auto& x : p.meterTrack.events)
                    if (x.id == id)
                        std::tie (x.numerator, x.denominator) = *m;
            });
        }, this);
        return;
    }
}

void MeterLane::addEventAt (int bar)
{
    const auto sig = document.getTempoMap().timeSignatureAtBar (bar);

    Dialogs::askText ("拍子変更の追加"_ju, juce::String (bar) + " 小節目からの拍子（例: 3/4, 6/8）"_ju,
                      juce::String (sig.numerator) + "/" + juce::String (sig.denominator),
                      [this, bar] (const juce::String& text)
    {
        auto m = parseMeter (text);

        if (! m)
            return Dialogs::showError ("拍子"_ju, "「4/4」のように入力してください（分母は 1, 2, 4, 8, 16, 32）。"_ju);

        document.perform ("拍子変更の追加"_ju, [bar, m] (collab::Project& p)
        {
            for (auto& x : p.meterTrack.events)
            {
                if (x.bar == bar)
                {
                    std::tie (x.numerator, x.denominator) = *m;
                    return;
                }
            }

            p.meterTrack.events.push_back ({ collab::generateUuid(), bar, m->first, m->second });
        });
    }, this);
}

void TempoLane::setGhost (double tick)
{
    if (std::abs (tick - ghostTick) > 0.5)
    {
        ghostTick = tick;
        repaint();
    }
}

void TempoLane::mouseExit (const juce::MouseEvent&)
{
    setGhost (-1.0);
}

void TempoLane::paintOverChildren (juce::Graphics& g)
{
    // 鉛筆ツール: クリックしたら置かれる位置
    if (ghostTick >= 0.0 && state.pencil())
        TimeGrid::drawPencilGhostLine (g, (float) state.timeline.tickToX (ghostTick), getHeight());
}

void MeterLane::setGhost (double tick)
{
    if (std::abs (tick - ghostTick) > 0.5)
    {
        ghostTick = tick;
        repaint();
    }
}

void MeterLane::mouseExit (const juce::MouseEvent&)
{
    setGhost (-1.0);
}

void MeterLane::paintOverChildren (juce::Graphics& g)
{
    // 鉛筆ツール: クリックしたら置かれる位置
    if (ghostTick >= 0.0 && state.pencil())
        TimeGrid::drawPencilGhostLine (g, (float) state.timeline.tickToX (ghostTick), getHeight());
}
