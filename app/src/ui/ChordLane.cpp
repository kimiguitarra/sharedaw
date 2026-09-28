#include "ChordLane.h"

#include "ChordEditor.h"
#include "TimeGrid.h"
#include "collab/ChordPlayback.h"
#include "collab/chord/Degree.h"
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
    setTooltip ("コード: 鉛筆ツールでクリックすると空のコードを置く。ダブルクリック（または選択して Enter）でコードを入力。"_ju
                "ドラッグで移動（1拍単位、Alt で自由）、Delete で削除。コードは置いた所で 1 回だけ鳴る（最長 1 小節）"_ju);
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

std::vector<ChordLane::Box> ChordLane::layoutBoxes() const
{
    const auto& axis = ctx.state.timeline;
    const auto& map = ctx.document.getTempoMap();
    const auto& project = ctx.document.getProject();

    auto events = project.chordTrack.events;
    std::stable_sort (events.begin(), events.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });

    const juce::Font nameFont (juce::FontOptions (15.5f, juce::Font::bold));
    const juce::Font degreeFont (juce::FontOptions (14.5f));
    std::vector<Box> boxes;

    for (size_t i = 0; i < events.size(); ++i)
    {
        const auto& e = events[i];
        Box b;
        b.id = e.id;
        b.name = displayText (e);
        b.empty = ! e.noChord && ! e.chord;
        b.noChord = e.noChord;

        if (e.chord && ! e.noChord)
            if (auto key = collab::keyAt (project, map, e.tick))
                b.degree = toJuce (collab::chord::degreeName (collab::toChord (*e.chord), *key));

        const float x = (float) axis.tickToX ((double) e.tick);
        float w = juce::jmax (juce::GlyphArrangement::getStringWidth (nameFont, b.name),
                              juce::GlyphArrangement::getStringWidth (degreeFont, b.degree)) + 14.0f;
        w = juce::jmax (b.empty ? 22.0f : 30.0f, w);

        // 鳴る長さ（次のコードまで、最長 1 小節）
        const auto oneBar = (collab::Tick) map.timeSignatureAtBar (map.tickToBar (e.tick)).ticksPerBar();
        auto stop = e.tick + oneBar;

        if (i + 1 < events.size())
        {
            stop = std::min (stop, events[i + 1].tick);
            w = juce::jmin (w, juce::jmax (8.0f, (float) axis.tickToX ((double) events[i + 1].tick) - x - 2.0f));
        }

        b.box = { x, 3.0f, w, (float) getHeight() - 9.0f };
        b.soundEndX = (float) axis.tickToX ((double) stop);
        boxes.push_back (b);
    }

    return boxes;
}

void ChordLane::paint (juce::Graphics& g)
{
    const auto& axis = ctx.state.timeline;
    const auto& map = ctx.document.getTempoMap();

    g.fillAll (Theme::laneAlt);
    TimeGrid::drawGrid (g, getLocalBounds(), axis, map, nullptr);

    for (auto& b : layoutBoxes())
    {
        if (b.soundEndX < 0 || b.box.getX() > (float) getWidth())
            continue;

        const bool selected = b.id == ctx.state.selectedChordId;
        const auto colour = b.noChord || b.empty ? Theme::textDim : chordColour;

        // 鳴っている長さ（細い線）
        if (! b.empty && ! b.noChord)
        {
            g.setColour (chordColour.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (b.box.getX(), (float) getHeight() - 5.0f, b.soundEndX - b.box.getX(), 2.0f));
        }

        g.setColour (colour.withAlpha (b.empty ? 0.1f : 0.25f));
        g.fillRoundedRectangle (b.box, 3.0f);
        g.setColour (selected ? Theme::selection : colour);
        g.drawRoundedRectangle (b.box, 3.0f, selected ? 2.0f : 1.0f);

        auto textArea = b.box.reduced (6.0f, 0.0f);

        if (b.degree.isNotEmpty())
        {
            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (15.5f, juce::Font::bold));
            g.drawText (b.name, textArea.removeFromTop (textArea.getHeight() * 0.55f), juce::Justification::bottomLeft, true);
            g.setColour (chordColour.brighter (0.3f));
            g.setFont (juce::FontOptions (14.5f));
            g.drawText (b.degree, textArea, juce::Justification::topLeft, true);
        }
        else
        {
            g.setColour (b.noChord || b.empty ? Theme::textDim : Theme::text);
            g.setFont (juce::FontOptions (15.5f, juce::Font::bold));
            g.drawText (b.empty ? juce::String ("?") : b.name, textArea, juce::Justification::centredLeft, true);
        }
    }
}

std::string ChordLane::findHit (float x) const
{
    for (auto& b : layoutBoxes())
        if (x >= b.box.getX() - 2.0f && x <= b.box.getRight() + 2.0f)
            return b.id;

    return {};
}

std::string ChordLane::findStartHit (float x) const
{
    return findHit (x);
}

void ChordLane::mouseMove (const juce::MouseEvent& e)
{
    setGhost (ctx.state.pencil() && findHit (e.position.x).empty() ? (double) snapToBeat (ctx.state.timeline.xToTick (e.position.x), e.mods) : -1.0);
    // コードは長さを持たないので、札の上でも普通の矢印（ドラッグで移動はできる）
    setMouseCursor (ctx.state.pencil() && findHit (e.position.x).empty() ? Theme::pencilCursor() : juce::MouseCursor::NormalCursor);
}

collab::Tick ChordLane::snapToBeat (double tick, const juce::ModifierKeys& mods) const
{
    const auto& map = ctx.document.getTempoMap();
    const auto t = (collab::Tick) std::llround (juce::jmax (0.0, tick));

    if (mods.isAltDown())
        return t;

    const auto sig = map.timeSignatureAtTick (t);
    return collab::Grid { sig.denominator, 1, true }.snap (t, map);
}

void ChordLane::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    mergeId = juce::Uuid().toString();

    // 鉛筆ツール: 空いている所をクリックしたら、その拍に空のコードを置く（Cubase と同じ。入力はダブルクリック）
    if (ctx.state.pencil() && ! e.mods.isPopupMenu() && findHit (e.position.x).empty())
    {
        dragId = addEmptyAt (snapToBeat (ctx.state.timeline.xToTick (e.position.x), e.mods));
        dragOrigTick = snapToBeat (ctx.state.timeline.xToTick (e.position.x), e.mods);
        dragDownTick = ctx.state.timeline.xToTick (e.position.x);
        return;
    }

    dragId = findHit (e.position.x);
    ctx.state.selectedChordId = dragId;
    ctx.state.changed();

    if (dragId.empty())
    {
        if (e.mods.isPopupMenu())
        {
            const auto tick = snapToBeat (ctx.state.timeline.xToTick (e.position.x), e.mods);
            juce::PopupMenu m;
            m.addItem ("ここにコードを入力…"_ju, [this, tick] { addAt (tick); });
            m.addItem ("ここに空のコードを置く"_ju, [this, tick] { addEmptyAt (tick); });
            m.addItem ("ここに貼り付け"_ju, hasClipboard(), false, [this, tick] { paste ((double) tick); });
            m.addSeparator();
            m.addItem ("コードを一括入力…"_ju, [this, tick] { showBulkDialog (ctx.document.getTempoMap().tickToBar (tick)); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        }

        return;
    }

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
        m.addSeparator();
        m.addItem ("コピー（Ctrl+C）"_ju, [this] { copySelected (false); });
        m.addItem ("切り取り（Ctrl+X）"_ju, [this] { copySelected (true); });
        m.addItem ("削除"_ju, [this] { deleteSelected(); });
        m.addSeparator();
        m.addItem ("コードを一括入力…"_ju, [this, x = e.position.x]
        {
            showBulkDialog (ctx.document.getTempoMap().tickToBar ((collab::Tick) ctx.state.timeline.xToTick (x)));
        });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
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
    // コードをダブルクリックしたらコードエディタ（鉛筆でも選択でも）
    if (auto id = findHit (e.position.x); ! id.empty())
        openEditor (id);
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

    // 数字 1〜7: 選んでいるコードにディグリーのコードを入れて、次のコードへ（「1625」と続けて打てる）
    if (const auto c = key.getTextCharacter(); c >= '1' && c <= '7' && ! ctx.state.selectedChordId.empty()
                                               && ! key.getModifiers().isAnyModifierKeyDown())
    {
        auto events = ctx.document.getProject().chordTrack.events;
        std::sort (events.begin(), events.end(), [] (auto& x, auto& y) { return x.tick < y.tick; });

        for (size_t i = 0; i < events.size(); ++i)
        {
            if (events[i].id != ctx.state.selectedChordId)
                continue;

            if (auto filled = chordForDegree (c, events[i].tick))
            {
                filled->id = events[i].id;
                ctx.document.perform ("コードの入力"_ju, [e = *filled] (collab::Project& p)
                {
                    for (auto& x : p.chordTrack.events)
                        if (x.id == e.id)
                            x = e;
                });
            }

            if (i + 1 < events.size())
                ctx.state.selectedChordId = events[i + 1].id;

            ctx.state.changed();
            return true;
        }
    }

    return false;
}

std::optional<collab::ChordEvent> ChordLane::chordForDegree (juce::juce_wchar digit, collab::Tick tick) const
{
    // キーがなければ C メジャーとして扱う
    const auto key = collab::keyAt (ctx.document.getProject(), ctx.document.getTempoMap(), tick)
                         .value_or (collab::chord::Key { 0, false });
    const auto text = collab::chord::degreeToChordText (juce::String::charToString (digit).toStdString(), key);

    if (! text)
        return std::nullopt;

    const auto parsed = collab::chord::parse (*text);

    if (! parsed.chord)
        return std::nullopt;

    collab::ChordEvent e;
    e.tick = tick;
    e.chord = collab::toChordSymbol (*parsed.chord);
    e.text = *text;
    return e;
}

void ChordLane::showBulkDialog (int bar)
{
    // 既定の終了: 曲の最後のクリップのある小節まで（なければ 8 小節）
    collab::Tick end = 0;

    for (auto& t : ctx.document.getProject().tracks)
    {
        for (auto& c : t.midiClips)
            end = std::max (end, c.endTick());
    }

    const int lastBar = juce::jmax (bar, ctx.document.getTempoMap().tickToBar (juce::jmax<collab::Tick> (0, end - 1)));
    auto* w = new juce::AlertWindow ("コードの一括入力"_ju,
                                     "範囲と間隔を決めて、空のコードをまとめて置きます。\n"_ju
                                     "「コード」に 1625 のようにディグリーの数字を並べると、前から順に入ります（空欄なら空のコードだけ）。"_ju,
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("from", juce::String (bar), "開始小節"_ju);
    w->addTextEditor ("to", juce::String (juce::jmax (bar + 7, juce::jmin (lastBar, bar + 15))), "終了小節（この小節まで）"_ju);
    w->addComboBox ("step", { "1 小節ごと"_ju, "2 拍ごと"_ju, "1 拍ごと"_ju, "2 小節ごと"_ju }, "間隔"_ju);
    w->addTextEditor ("degrees", {}, "コード（例: 1625）"_ju);
    w->addButton ("置く"_ju, 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("キャンセル"_ju, 0, juce::KeyPress (juce::KeyPress::escapeKey));

    for (auto* name : { "from", "to" })
        if (auto* ed = w->getTextEditor (name))
            ed->setInputRestrictions (4, "0123456789");

    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, safe = juce::Component::SafePointer<ChordLane> (this)] (int result)
    {
        if (result != 1 || safe == nullptr)
            return;

        const int from = juce::jmax (1, w->getTextEditorContents ("from").getIntValue());
        const int to = juce::jmax (from, w->getTextEditorContents ("to").getIntValue());
        const int step = w->getComboBoxComponent ("step")->getSelectedItemIndex();
        const int beats = step == 1 ? 2 : step == 2 ? 1 : 0;
        const int bars = step == 3 ? 2 : 1;
        bulkFill (from, to, beats, bars, w->getTextEditorContents ("degrees"));
    }), true);
}

void ChordLane::bulkFill (int fromBar, int toBar, int beatsPerStep, int barsPerStep, const juce::String& degrees)
{
    const auto& map = ctx.document.getTempoMap();
    std::vector<collab::Tick> ticks;

    for (int bar = fromBar; bar <= toBar; bar += (beatsPerStep > 0 ? 1 : barsPerStep))
    {
        const auto start = map.barToTick (bar);

        if (beatsPerStep <= 0)
        {
            ticks.push_back (start);
            continue;
        }

        const auto sig = map.timeSignatureAtBar (bar);

        for (int beat = 0; beat < sig.numerator; beat += beatsPerStep)
            ticks.push_back (start + (collab::Tick) beat * sig.ticksPerBeat());
    }

    // 数字（1〜7、全角も）だけを順に使う
    std::vector<juce::juce_wchar> digits;

    for (auto c : degrees)
    {
        if (c >= 0xFF11 && c <= 0xFF17)
            c = '1' + (c - 0xFF11);

        if (c >= '1' && c <= '7')
            digits.push_back (c);
    }

    // すでにコードがある拍はそのまま（空のコードなら数字で埋める）
    std::vector<collab::ChordEvent> toAdd, toFill;
    size_t next = 0;
    const auto& existing = ctx.document.getProject().chordTrack.events;

    for (auto t : ticks)
    {
        auto it = std::find_if (existing.begin(), existing.end(), [t] (auto& x) { return x.tick == t; });
        const bool isEmpty = it == existing.end() || (! it->noChord && ! it->chord && it->text.empty());

        if (it != existing.end() && ! isEmpty)
            continue;

        collab::ChordEvent e;
        e.tick = t;

        if (next < digits.size())
            if (auto filled = chordForDegree (digits[next++], t))
                e = *filled;

        e.id = it != existing.end() ? it->id : collab::generateUuid();
        (it != existing.end() ? toFill : toAdd).push_back (e);
    }

    if (toAdd.empty() && toFill.empty())
        return;

    ctx.document.perform ("コードの一括入力"_ju, [toAdd, toFill] (collab::Project& p)
    {
        for (auto& e : toFill)
            for (auto& x : p.chordTrack.events)
                if (x.id == e.id)
                    x = e;

        for (auto& e : toAdd)
            p.chordTrack.events.push_back (e);
    });

    // 最初の空のコードを選んでおく（続けて数字で入れられる）
    std::string firstEmpty;
    collab::Tick firstTick = std::numeric_limits<collab::Tick>::max();

    for (auto& e : ctx.document.getProject().chordTrack.events)
        if (e.tick >= map.barToTick (fromBar) && e.tick < firstTick && ! e.noChord && ! e.chord && e.text.empty())
        {
            firstTick = e.tick;
            firstEmpty = e.id;
        }

    ctx.state.selectedChordId = firstEmpty;
    ctx.state.changed();

    // ダイアログが閉じた後にフォーカスを取り戻す（続けて数字を打てるように）
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ChordLane> (this)]
    {
        if (safe != nullptr)
            safe->grabKeyboardFocus();
    });
}

bool ChordLane::copySelected (bool cut)
{
    for (auto& ev : ctx.document.getProject().chordTrack.events)
        if (ev.id == ctx.state.selectedChordId)
        {
            clipboard = ev;

            if (cut)
                deleteSelected();

            return true;
        }

    return false;
}

bool ChordLane::paste (double playheadTick)
{
    if (! clipboard)
        return false;

    const auto tick = snapToBeat (playheadTick, {});
    auto e = *clipboard;
    std::string id = collab::generateUuid();

    // その拍に既にコードがあれば、中身を置き換える
    for (auto& ev : ctx.document.getProject().chordTrack.events)
        if (ev.tick == tick)
            id = ev.id;

    e.id = id;
    e.tick = tick;

    ctx.document.perform ("コードの貼り付け"_ju, [e] (collab::Project& p)
    {
        for (auto& x : p.chordTrack.events)
            if (x.id == e.id)
            {
                x = e;
                return;
            }

        p.chordTrack.events.push_back (e);
    });

    ctx.state.selectedChordId = e.id;
    ctx.state.changed();
    return true;
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
        ChordEditor::show (displayText (ev), collab::keyAt (ctx.document.getProject(), ctx.document.getTempoMap(), ev.tick),
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

std::string ChordLane::addEmptyAt (collab::Tick tick)
{
    for (auto& ev : ctx.document.getProject().chordTrack.events)
        if (ev.tick == tick)
        {
            ctx.state.selectedChordId = ev.id;
            ctx.state.changed();
            return ev.id;
        }

    collab::ChordEvent e;
    e.id = collab::generateUuid();
    e.tick = tick;

    ctx.document.perform ("コードの追加"_ju, [e] (collab::Project& p) { p.chordTrack.events.push_back (e); });
    ctx.state.selectedChordId = e.id;
    ctx.state.changed();
    return e.id;
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

    ChordEditor::show (initial, collab::keyAt (ctx.document.getProject(), ctx.document.getTempoMap(), tick),
                       [doc, state, tick] (ChordEditor::Result r)
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

void ChordLane::setGhost (double tick)
{
    if (std::abs (tick - ghostTick) > 0.5)
    {
        ghostTick = tick;
        repaint();
    }
}

void ChordLane::mouseExit (const juce::MouseEvent&)
{
    setGhost (-1.0);
}

void ChordLane::paintOverChildren (juce::Graphics& g)
{
    // 鉛筆ツール: クリックしたら置かれる位置
    if (ghostTick >= 0.0 && ctx.state.pencil())
        TimeGrid::drawPencilGhostBox (g, { (float) ctx.state.timeline.tickToX (ghostTick), 3.0f, 34.0f, (float) getHeight() - 9.0f });
}
