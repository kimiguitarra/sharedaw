// ピアノロールのノートを置く所（表示・マウス・キーボードでの編集）

#include "PianoRoll.h"
#include "PianoRollDetail.h"

#include "TimeGrid.h"
#include "audio/AudioFiles.h"
#include "collab/ClipEditing.h"
#include "collab/GmDrumMap.h"
#include "collab/Uuid.h"

using namespace PianoRollDetail;

//==============================================================================
NoteGrid::NoteGrid (PianoRollView& o) : owner (o)
{
    setWantsKeyboardFocus (true);
}

const collab::Note* NoteGrid::hitNote (juce::Point<float> p, bool& nearRightEdge) const
{
    nearRightEdge = false;
    auto* clip = owner.getClip();

    if (clip == nullptr)
        return nullptr;

    const auto& axis = owner.axis();

    for (auto it = clip->notes.rbegin(); it != clip->notes.rend(); ++it)
    {
        const float x1 = (float) axis.tickToX ((double) (clip->startTick + it->tick));
        float x2 = (float) axis.tickToX ((double) (clip->startTick + it->endTick()));
        const float y = owner.pitchToY (it->pitch);

        // ドラムは、描いているブロック（グリッド 1 マス分）の大きさで当たりを取る
        if (! owner.drumRows.empty())
            x2 = x1 + juce::jlimit (6.0f, 40.0f, (float) (juce::jmax<collab::Tick> (30, owner.ctx.state.grid.stepTicks()) * axis.pixelsPerTick()) - 2.0f);

        if (p.x >= x1 && p.x <= juce::jmax (x2, x1 + 3.0f) && p.y >= y && p.y < y + (float) owner.noteHeight)
        {
            nearRightEdge = owner.drumRows.empty() && x2 - p.x <= edgeGrab && x2 - x1 > edgeGrab * 1.5f;   // ドラムは長さを変えない
            return &*it;
        }
    }

    return nullptr;
}

void NoteGrid::paint (juce::Graphics& g)
{
    const auto& axis = owner.axis();
    const auto& map = owner.ctx.document.getTempoMap();
    auto* clip = owner.getClip();
    auto* track = owner.getTrack();
    const bool drums = owner.isDrumTrack();

    g.fillAll (Theme::lane);

    for (int p = 0; p < 128; ++p)
    {
        const float y = owner.pitchToY (p);

        if (y + (float) owner.noteHeight < 0 || y > (float) getHeight())
            continue;

        if (drums)
        {
            // 行ごとに縞、仲間の境目は太い線
            const int rowIndex = owner.rowOfPitch (p);
            g.setColour (owner.drumPieceName (p).isEmpty() ? Theme::laneAlt.darker (0.2f) : (rowIndex % 2 == 0 ? Theme::lane : Theme::laneAlt));
            g.fillRect (0.0f, y, (float) getWidth(), (float) owner.noteHeight);
            g.setColour (Theme::gridSub);
            g.drawHorizontalLine ((int) (y + (float) owner.noteHeight) - 1, 0.0f, (float) getWidth());

            if (rowIndex + 1 < (int) owner.drumRows.size() && owner.drumFamilyOf (owner.drumRows[(size_t) rowIndex + 1]) != owner.drumFamilyOf (p))
            {
                g.setColour (Theme::gridBar);
                g.fillRect (0.0f, y + (float) owner.noteHeight - 2.0f, (float) getWidth(), 2.0f);
            }

            continue;
        }

        g.setColour (isBlackKey (p) ? Theme::laneAlt : Theme::lane);
        g.fillRect (0.0f, y, (float) getWidth(), (float) owner.noteHeight);

        if (p % 12 == 0)
        {
            g.setColour (Theme::gridBeat);
            g.drawHorizontalLine ((int) (y + (float) owner.noteHeight) - 1, 0.0f, (float) getWidth());
        }
    }

    TimeGrid::drawGrid (g, getLocalBounds(), axis, map, &owner.ctx.state.grid);

    if (clip == nullptr)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (16.5f));
        g.drawText (owner.canCreateClip() ? "鉛筆ツールでクリックすると、クリップを作ってノートを置きます"_ju
                                          : "MIDI トラックかクリップを選ぶと、ここで編集できます"_ju,
                    getLocalBounds(), juce::Justification::centred);

        // 鉛筆の置き場所の影（クリップがなくても出す）
        if (ghostPitch >= 0 && ghostTick >= 0.0)
        {
            g.setColour (Theme::accent.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> ((float) axis.tickToX (ghostTick), owner.pitchToY (ghostPitch),
                                                (float) (owner.ctx.state.grid.stepTicks() * axis.pixelsPerTick()), (float) owner.noteHeight));
        }

        return;
    }

    // クリップの外は暗くする
    const float cx1 = (float) axis.tickToX ((double) clip->startTick);
    const float cx2 = (float) axis.tickToX ((double) clip->endTick());
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, juce::jmax (0.0f, cx1), (float) getHeight()));
    g.fillRect (juce::Rectangle<float> (cx2, 0.0f, (float) getWidth() - cx2, (float) getHeight()));

    const auto base = track != nullptr ? Theme::parseColour (track->color) : Theme::accent;

    for (auto& n : clip->notes)
    {
        const float x1 = (float) axis.tickToX ((double) (clip->startTick + n.tick));
        const float x2 = (float) axis.tickToX ((double) (clip->startTick + n.endTick()));
        const float y = owner.pitchToY (n.pitch);

        if (x2 < 0 || x1 > (float) getWidth() || y + (float) owner.noteHeight < 0 || y > (float) getHeight())
            continue;

        const bool selected = owner.selectedNotes.count (n.id) > 0;
        const bool outside = n.tick < 0 || n.tick >= clip->lengthTick;
        const bool silent = drums && owner.drumPieceName (n.pitch).isEmpty();   // キットにない音（鳴らない）

        if (drums)
        {
            // ドラム: 叩いた場所を、グリッド 1 マス分のブロックで（強さ = 濃さ・高さ）
            const float step = (float) (juce::jmax<collab::Tick> (30, owner.ctx.state.grid.stepTicks()) * axis.pixelsPerTick());
            const float w = juce::jlimit (6.0f, 40.0f, step - 2.0f);
            const float h = ((float) owner.noteHeight - 6.0f) * (0.45f + 0.55f * (float) n.velocity / 127.0f);
            const auto hit = juce::Rectangle<float> (x1 + 1.0f, y + ((float) owner.noteHeight - h) * 0.5f, w, h);

            if (selected)
            {
                g.setColour (juce::Colour (0xffff7a1a).withAlpha (0.3f));
                g.fillRoundedRectangle (hit.expanded (4.5f), 6.0f);
            }

            g.setColour (velocityColour (n.velocity, base).withAlpha (outside || silent ? 0.3f : 1.0f));
            g.fillRoundedRectangle (hit, 3.0f);
            g.setColour (Theme::overlay (0.25f));
            g.drawRoundedRectangle (hit, 3.0f, 1.0f);

            // 選択中は外側を暖色（オレンジ）の太い枠で囲う（ブロックが小さくても選ばれているのが分かるように）
            if (selected)
            {
                const juce::Colour warm (0xffff7a1a);
                g.setColour (warm);
                g.drawRoundedRectangle (hit.expanded (2.0f), 4.5f, 2.5f);
            }

            if (silent)
            {
                g.setColour (Theme::warning);
                g.drawLine (hit.getX(), hit.getBottom(), hit.getRight(), hit.getY(), 1.0f);
            }

            // ベロシティの数値（ブロックに収まる幅があるとき）
            if (w >= 17.0f && owner.noteHeight >= 12)
            {
                const auto textArea = juce::Rectangle<float> (hit.getX(), y, w, (float) owner.noteHeight);
                const auto text = juce::String (n.velocity);
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.setColour (Theme::background.withAlpha (0.85f));
                g.drawText (text, textArea.translated (0.8f, 0.8f), juce::Justification::centred, false);
                g.setColour (Theme::text);
                g.drawText (text, textArea, juce::Justification::centred, false);
            }

            continue;
        }

        const auto r = juce::Rectangle<float> (x1, y + 1.0f, juce::jmax (3.0f, x2 - x1), (float) owner.noteHeight - 2.0f);
        g.setColour (velocityColour (n.velocity, base).withAlpha (outside ? 0.3f : 1.0f));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (selected ? Theme::selection : juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 2.0f, selected ? 2.0f : 1.0f);

        // 音名（C4 など）をノートの左端に
        if (r.getWidth() >= 20.0f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (toJuce (collab::midiNoteName (n.pitch)), r.withTrimmedLeft (3.0f), juce::Justification::centredLeft, false);
        }
    }

    if (mode == Mode::rubberBand)
    {
        g.setColour (Theme::accent.withAlpha (0.2f));
        g.fillRect (rubberBand);
        g.setColour (Theme::accent);
        g.drawRect (rubberBand, 1.0f);
    }

    // 鉛筆で置かれるノート
    if (ghostPitch >= 0 && owner.ctx.state.pencil() && mode == Mode::none && ! drawingNote)
    {
        const auto step = (double) juce::jmax<collab::Tick> (10, owner.ctx.state.grid.stepTicks());
        const float x1 = (float) axis.tickToX (ghostTick), x2 = (float) axis.tickToX (ghostTick + step);
        TimeGrid::drawPencilGhostBox (g, { x1, owner.pitchToY (ghostPitch) + 1.0f, juce::jmax (4.0f, x2 - x1), (float) owner.noteHeight - 2.0f }, false);
    }

    // はさみで切る位置
    if (splitX >= 0.0 && owner.ctx.state.tool == EditTool::split)
    {
        g.setColour (Theme::selection);
        g.fillRect (juce::Rectangle<float> ((float) splitX - 0.5f, splitY - 3.0f, 1.5f, (float) owner.noteHeight + 6.0f));
    }
}

void NoteGrid::mouseExit (const juce::MouseEvent&)
{
    if (splitX >= 0.0 || ghostPitch >= 0)
    {
        splitX = -1.0;
        ghostPitch = -1;
        repaint();
    }
}

void NoteGrid::mouseMove (const juce::MouseEvent& e)
{
    // 鉛筆: 空いている所なら、クリックで置かれるノートの枠
    {
        bool onEdge = false;
        auto* clip = owner.getClip();
        const bool canCreate = owner.canCreateClip();
        const bool pencilFree = owner.ctx.state.pencil() && (clip != nullptr || canCreate) && hitNote (e.position, onEdge) == nullptr;
        double tick = -1.0;
        int pitch = -1;

        if (pencilFree)
        {
            const auto abs = owner.snap (owner.axis().xToTick (e.position.x), true, e.mods);

            if (canCreate || (abs >= clip->startTick && abs < clip->endTick()))
            {
                tick = (double) abs;
                pitch = owner.yToPitch (e.position.y);
            }
        }

        if (std::abs (tick - ghostTick) > 0.5 || pitch != ghostPitch)
        {
            ghostTick = tick;
            ghostPitch = pitch;
            repaint();
        }
    }

    // はさみ: ノートの上なら切る位置に縦線
    {
        bool onEdge = false;
        auto* note = owner.ctx.state.tool == EditTool::split ? hitNote (e.position, onEdge) : nullptr;
        const double x = note != nullptr ? owner.axis().tickToX ((double) owner.snap (owner.axis().xToTick (e.position.x), false, e.mods)) : -1.0;
        const float y = note != nullptr ? owner.pitchToY (note->pitch) : -1.0f;

        if (std::abs (x - splitX) > 0.1 || std::abs (y - splitY) > 0.1f)
        {
            splitX = x;
            splitY = y;
            repaint();
        }
    }

    bool edge = false;
    auto* n = hitNote (e.position, edge);

    // ノートの上ではベロシティを出す
    {
        juce::String tip;

        if (n != nullptr)
        {
            auto name = owner.isDrumTrack() ? owner.drumPieceName (n->pitch) : juce::String();
            tip = (name.isNotEmpty() ? name : toJuce (collab::midiNoteName (n->pitch))) + "  ベロシティ "_ju + juce::String (n->velocity);
        }

        if (tip != getTooltip())
            setTooltip (tip);
    }

    if (owner.ctx.state.tool != EditTool::select)
        return setMouseCursor (Theme::toolCursor (owner.ctx.state.tool));


    setMouseCursor (n != nullptr && edge ? juce::MouseCursor::LeftRightResizeCursor
                                         : n != nullptr ? juce::MouseCursor::DraggingHandCursor
                                                        : juce::MouseCursor::NormalCursor);
}

void NoteGrid::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    auto* clip = owner.getClip();
    mode = Mode::none;

    // 鉛筆で空いている所に書いたら、MIDI クリップを自動で作る（または選択中のクリップを伸ばす）
    if (owner.ctx.state.pencil() && ! e.mods.isPopupMenu() && owner.canCreateClip())
    {
        bool onEdge = false;
        const auto abs = owner.snap (owner.axis().xToTick (e.position.x), true, e.mods);

        if ((clip == nullptr || abs < clip->startTick || abs >= clip->endTick()) && hitNote (e.position, onEdge) == nullptr)
            clip = owner.ensureClipAt (abs);
    }

    if (clip == nullptr)
        return;

    bool edge = false;
    auto* n = hitNote (e.position, edge);
    mergeId = juce::Uuid().toString();
    const auto tool = owner.ctx.state.tool;

    // はさみ: クリックした位置でノートを 2 つに分ける
    if (tool == EditTool::split && n != nullptr && ! e.mods.isPopupMenu())
    {
        const auto at = owner.snap (owner.axis().xToTick (e.position.x), false, e.mods) - clip->startTick;
        const auto id = n->id;

        if (at > n->tick && at < n->endTick())
        {
            auto second = *n;
            second.id = collab::generateUuid();
            second.tick = at;
            second.lengthTick = n->endTick() - at;

            owner.editNotes ("ノートの分割"_ju, [id, at, second] (collab::MidiClip& c)
            {
                for (auto& x : c.notes)
                    if (x.id == id)
                        x.lengthTick = at - x.tick;

                c.notes.push_back (second);
            });
        }

        return;
    }


    if (owner.ctx.state.pencil() && ! e.mods.isPopupMenu())
    {
        // 鉛筆ツール: ノートをクリックしたら消す（Cubase のキーエディターと同じ）、空いている所なら置く
        if (n != nullptr)
        {
            if (owner.ctx.state.behaviour().pencilClickOnNoteDeletes)
            {
                auto id = n->id;
                owner.selectedNotes.erase (id);
                owner.editNotes ("ノートの削除"_ju, [id] (collab::MidiClip& c)
                {
                    c.notes.erase (std::remove_if (c.notes.begin(), c.notes.end(), [&] (auto& x) { return x.id == id; }), c.notes.end());
                });
            }

            return;
        }

        const auto abs = owner.snap (owner.axis().xToTick (e.position.x), true, e.mods);

        if (abs < clip->startTick || abs >= clip->endTick())
            return;

        const auto step = owner.ctx.state.grid.stepTicks();
        collab::Note note;
        note.id = collab::generateUuid();
        note.tick = abs - clip->startTick;
        note.lengthTick = juce::jmax<collab::Tick> (10, e.mods.isAltDown() || step <= 0 ? owner.lastNoteLength : step);
        note.pitch = owner.yToPitch (e.position.y);
        note.velocity = owner.lastVelocity;

        owner.editNotes ("ノートの追加"_ju, [note] (collab::MidiClip& c) { c.notes.push_back (note); }, mergeId);
        owner.selectedNotes = { note.id };
        owner.previewNote (note.pitch, note.velocity);

        // そのまま右へドラッグすると長さを変えられる（追加と同じ 1 回の操作として元に戻る）
        mode = Mode::resize;
        anchorId = note.id;
        downTick = (double) (clip->startTick + note.tick + note.lengthTick);
        downPitch = note.pitch;
        originals = { { note.id, { note.tick, note.lengthTick, note.pitch } } };
        drawingNote = true;
        return;
    }

    if (n == nullptr)
    {
        if (! e.mods.isShiftDown())
            owner.selectedNotes.clear();

        mode = Mode::rubberBand;
        rubberBand = { e.position, e.position };
        repaint();
        return;
    }

    if (e.mods.isShiftDown())
    {
        if (owner.selectedNotes.count (n->id) > 0)
        {
            owner.selectedNotes.erase (n->id);
            repaint();
            return;
        }

        owner.selectedNotes.insert (n->id);
    }
    else if (owner.selectedNotes.count (n->id) == 0)
    {
        owner.selectedNotes = { n->id };
    }

    owner.lastNoteLength = n->lengthTick;
    owner.lastVelocity = n->velocity;
    owner.previewNote (n->pitch, n->velocity);

    mode = edge ? Mode::resize : Mode::move;
    anchorId = n->id;
    downTick = owner.axis().xToTick (e.position.x);
    downPitch = owner.yToPitch (e.position.y);
    originals.clear();

    for (auto& note : clip->notes)
        if (owner.selectedNotes.count (note.id) > 0)
            originals[note.id] = { note.tick, note.lengthTick, note.pitch };

    owner.repaint();
}

void NoteGrid::mouseDrag (const juce::MouseEvent& e)
{
    auto* clip = owner.getClip();

    if (clip == nullptr)
        return;


    if (mode == Mode::rubberBand)
    {
        rubberBand = juce::Rectangle<float> (e.mouseDownPosition, e.position);

        // 範囲内のノートを選択
        const auto& axis = owner.axis();
        std::set<std::string> sel = e.mods.isShiftDown() ? owner.selectedNotes : std::set<std::string>();

        for (auto& n : clip->notes)
        {
            const auto r = juce::Rectangle<float> ((float) axis.tickToX ((double) (clip->startTick + n.tick)), owner.pitchToY (n.pitch),
                                                   juce::jmax (3.0f, (float) (n.lengthTick * axis.pixelsPerTick())), (float) owner.noteHeight);
            if (r.intersects (rubberBand))
                sel.insert (n.id);
        }

        owner.selectedNotes = sel;
        owner.repaint();
        return;
    }

    if ((mode != Mode::move && mode != Mode::resize) || (e.getDistanceFromDragStart() < 2 && ! drawingNote))
        return;

    auto anchor = originals.find (anchorId);

    if (anchor == originals.end())
        return;

    const double delta = owner.axis().xToTick (e.position.x) - downTick;
    const auto clipStart = clip->startTick;
    const auto clipLength = clip->lengthTick;
    auto origs = originals;

    if (mode == Mode::move)
    {
        const auto anchorAbs = clipStart + anchor->second.tick;
        const auto newAbs = owner.snap ((double) anchorAbs + delta, false, e.mods);
        auto dt = newAbs - anchorAbs;
        // ドラムは行（音色）単位で動かす。それ以外は半音単位
        const auto& rows = owner.drumRows;
        int dp = rows.empty() ? owner.yToPitch (e.position.y) - downPitch
                              : owner.rowOfPitch (owner.yToPitch (e.position.y)) - owner.rowOfPitch (downPitch);

        // 全ノートがクリップ・音域（行）の範囲に収まるように制限する
        for (auto& [id, o] : origs)
        {
            dt = juce::jlimit (-o.tick, clipLength - 1 - o.tick, dt);

            if (rows.empty())
                dp = juce::jlimit (-o.pitch, 127 - o.pitch, dp);
            else if (const int r = owner.rowOfPitch (o.pitch); r >= 0)
                dp = juce::jlimit (-r, (int) rows.size() - 1 - r, dp);
        }

        owner.editNotes ("ノートの移動"_ju, [origs, dt, dp, rows, &owner = owner] (collab::MidiClip& c)
        {
            for (auto& n : c.notes)
                if (auto it = origs.find (n.id); it != origs.end())
                {
                    n.tick = it->second.tick + dt;

                    if (rows.empty())
                        n.pitch = it->second.pitch + dp;
                    else if (const int r = owner.rowOfPitch (it->second.pitch); r >= 0)
                        n.pitch = rows[(size_t) juce::jlimit (0, (int) rows.size() - 1, r + dp)];
                }
        }, mergeId);
    }
    else
    {
        const auto anchorAbsEnd = clipStart + anchor->second.tick + anchor->second.length;
        const auto newEnd = owner.snap ((double) anchorAbsEnd + delta, false, e.mods);
        const auto dl = newEnd - anchorAbsEnd;
        const auto minLen = e.mods.isAltDown() ? (collab::Tick) 10 : juce::jmax<collab::Tick> (10, owner.ctx.state.grid.stepTicks());

        owner.editNotes ("ノートの長さ変更"_ju, [origs, dl, minLen] (collab::MidiClip& c)
        {
            for (auto& n : c.notes)
                if (auto it = origs.find (n.id); it != origs.end())
                    n.lengthTick = juce::jmax (juce::jmin (minLen, it->second.length), it->second.length + dl);
        }, mergeId);
    }
}

void NoteGrid::mouseUp (const juce::MouseEvent&)
{
    if (mode == Mode::rubberBand)
        repaint();

    // 長さを変えたら、次に追加するノートの長さとして覚えておく
    if (auto* clip = owner.getClip(); clip != nullptr && mode == Mode::resize)
        for (auto& n : clip->notes)
            if (n.id == anchorId)
                owner.lastNoteLength = n.lengthTick;

    mode = Mode::none;
    drawingNote = false;
    owner.ctx.document.endMerge();
}

void NoteGrid::mouseDoubleClick (const juce::MouseEvent&)
{
    // 追加・削除は鉛筆ツールで行う（選択ツールのダブルクリックでは何もしない）
}

void NoteGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    owner.handleWheel (e, w, this);
}

bool NoteGrid::keyPressed (const juce::KeyPress& key)
{
    auto* clip = owner.getClip();

    if (clip == nullptr)
        return false;

    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();
    const bool arrow = code == juce::KeyPress::upKey || code == juce::KeyPress::downKey
                    || code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;

    // Esc: 選択を外す
    if (code == juce::KeyPress::escapeKey && ! owner.selectedNotes.empty())
    {
        owner.selectedNotes.clear();
        owner.repaint();
        return true;
    }

    // Ctrl（Mac は Cmd）+ Alt + ←→: ナッジ（選んだノートを少しだけずらす）
    if (mods.isAltDown() && mods.isCommandDown() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
    {
        owner.nudgeSelection (code == juce::KeyPress::leftKey ? -1 : 1);
        return true;
    }

    // Alt + ↑↓: 選んだノートのベロシティを 1（Shift も押すと 10）上げ下げ
    if (mods.isAltDown() && ! owner.selectedNotes.empty() && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
    {
        owner.changeSelectedVelocity ((code == juce::KeyPress::upKey ? 1 : -1) * (mods.isShiftDown() ? 10 : 1));
        return true;
    }

    // Alt + ←→: 前後のノートを選ぶ（Shift も押すと選択に足す）。何も選んでいなければ再生位置から
    if (arrow && mods.isAltDown() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
    {
        std::vector<const collab::Note*> notes;

        for (auto& n : clip->notes)
            notes.push_back (&n);

        std::sort (notes.begin(), notes.end(), [] (auto* a, auto* b) { return a->tick != b->tick ? a->tick < b->tick : a->pitch > b->pitch; });

        if (notes.empty())
            return true;

        const bool forward = code == juce::KeyPress::rightKey;
        const collab::Note* pick = nullptr;

        // 選んでいるノートがあれば、その（最後の）ノートの隣。なければ再生位置の前後
        int index = -1;

        for (int k = 0; k < (int) notes.size(); ++k)
            if (owner.selectedNotes.count (notes[(size_t) k]->id) > 0)
                index = forward || index < 0 ? k : index;

        if (index >= 0)
        {
            const int next = juce::jlimit (0, (int) notes.size() - 1, index + (forward ? 1 : -1));
            pick = notes[(size_t) next];
        }
        else
        {
            const double from = owner.ctx.state.playheadTick - (double) clip->startTick;

            for (auto* n : notes)
            {
                if (forward && (double) n->tick >= from - 0.5)
                {
                    pick = n;
                    break;
                }

                if (! forward && (double) n->tick < from - 0.5)
                    pick = n;
            }
        }

        if (pick != nullptr)
        {
            if (! mods.isShiftDown())
                owner.selectedNotes.clear();

            owner.selectedNotes.insert (pick->id);
            owner.previewNote (pick->pitch, pick->velocity);
            owner.repaint();
        }

        return true;
    }

    if (owner.selectedNotes.empty())
        return false;

    const auto sel = owner.selectedNotes;

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const int dir = code == juce::KeyPress::upKey ? 1 : -1;

        if (! owner.drumRows.empty())
        {
            // ドラム: 画面で上・下の行（音色）へ動かす（ピッチの数字の順ではない）
            const auto rows = owner.drumRows;
            owner.editNotes ("ノートの移動（音色）"_ju, [sel, rows, dir] (collab::MidiClip& c)
            {
                for (auto& n : c.notes)
                    if (sel.count (n.id) > 0)
                    {
                        auto it = std::find (rows.begin(), rows.end(), n.pitch);

                        if (it == rows.end())
                            continue;

                        const int row = juce::jlimit (0, (int) rows.size() - 1, (int) (it - rows.begin()) - dir);
                        n.pitch = rows[(size_t) row];
                    }
            });
        }
        else
        {
            // Shift または Ctrl と一緒ならオクターブ
            const bool octave = mods.isShiftDown() || mods.isCommandDown();
            const int d = dir * (octave ? 12 : 1);
            owner.editNotes ("ノートの移調"_ju, [sel, d] (collab::MidiClip& c)
            {
                for (auto& n : c.notes)
                    if (sel.count (n.id) > 0)
                        n.pitch = juce::jlimit (0, 127, n.pitch + d);
            });
        }

        // 動かした音を鳴らす（いちばん早いノート）
        if (auto* c = owner.getClip())
        {
            const collab::Note* first = nullptr;

            for (auto& n : c->notes)
                if (sel.count (n.id) > 0 && (first == nullptr || n.tick < first->tick))
                    first = &n;

            if (first != nullptr)
                owner.previewNote (first->pitch, first->velocity);
        }

        return true;
    }

    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const int dir = code == juce::KeyPress::leftKey ? -1 : 1;
        const auto& map = owner.ctx.document.getTempoMap();
        const auto grid = std::max<collab::Tick> (1, owner.ctx.state.grid.stepTicks());

        // Ctrl（Mac は Cmd）+ ←→: 長さをグリッド 1 つ分縮める・伸ばす
        if (mods.isCommandDown())
        {
            owner.editNotes ("ノートの長さ"_ju, [sel, grid, dir] (collab::MidiClip& c)
            {
                for (auto& n : c.notes)
                    if (sel.count (n.id) > 0)
                        n.lengthTick = juce::jlimit<collab::Tick> (std::max<collab::Tick> (1, grid / 4), c.lengthTick - n.tick, n.lengthTick + dir * grid);
            });
            return true;
        }

        // ←→: グリッド 1 つ分、Shift + ←→: 1 小節分動かす
        const auto step = mods.isShiftDown() ? map.timeSignatureAtTick (clip->startTick).ticksPerBar() : grid;
        owner.editNotes ("ノートの移動"_ju, [sel, step, dir] (collab::MidiClip& c)
        {
            for (auto& n : c.notes)
                if (sel.count (n.id) > 0)
                    n.tick = juce::jlimit<collab::Tick> (0, c.lengthTick - 1, n.tick + dir * step);
        });
        return true;
    }

    return false;
}
