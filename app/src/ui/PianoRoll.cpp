#include "PianoRoll.h"

#include "TimeGrid.h"
#include "audio/AudioFiles.h"
#include "collab/ClipEditing.h"
#include "collab/GmDrumMap.h"
#include "collab/Uuid.h"

namespace
{
    /** ドラムの音の仲間（行の区切り線用）: 0 キック, 1 スネア, 2 ハイハット, 3 タム, 4 シンバル, 5 その他 */
    int drumFamily (int pitch)
    {
        switch (pitch)
        {
            case 35: case 36:                                   return 0;
            case 37: case 38: case 39: case 40:                 return 1;
            case 42: case 44: case 46:                          return 2;
            case 41: case 43: case 45: case 47: case 48: case 50: return 3;
            case 49: case 51: case 52: case 53: case 55: case 57: case 59: return 4;
            default:                                            return 5;
        }
    }

    constexpr int toolbarHeight = 30;
    constexpr int rulerHeight = 24;
    constexpr int velocityHeight = 70;
    constexpr int scrollBarSize = 12;
    constexpr float edgeGrab = 6.0f;

    bool isBlackKey (int pitch)
    {
        const int n = pitch % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    juce::Colour velocityColour (int velocity, juce::Colour base)
    {
        return base.withMultipliedBrightness (0.55f + 0.45f * (float) velocity / 127.0f);
    }
}

//==============================================================================
void PianoKeyboard::paint (juce::Graphics& g)
{
    const bool drums = owner.isDrumTrack();
    g.fillAll (Theme::panel);
    g.setFont (juce::FontOptions (14.0f));

    for (int p = 0; p < 128; ++p)
    {
        const float y = owner.pitchToY (p);

        if (y + (float) owner.noteHeight < 0 || y > (float) getHeight())
            continue;

        const auto row = juce::Rectangle<float> (0.0f, y, (float) getWidth(), (float) owner.noteHeight);

        if (drums)
        {
            // EZ Drummer のように、音色の名前を大きく。キットにない音（ノートだけある）は暗く「音なし」
            const auto name = owner.drumPieceName (p);
            const auto gmName = toJuce (collab::gmDrumName (p));
            const int rowIndex = owner.rowOfPitch (p);
            g.setColour (name.isEmpty() ? Theme::panel : (rowIndex % 2 == 0 ? Theme::panelLight : Theme::panelLight.darker (0.12f)));
            g.fillRect (row.reduced (0.0f, 0.5f));
            g.setColour (name.isNotEmpty() ? Theme::text : Theme::textDim.withAlpha (0.7f));
            g.setFont (juce::FontOptions (16.0f, name.isNotEmpty() ? juce::Font::bold : juce::Font::plain));
            g.drawText (name.isNotEmpty() ? name : (gmName.isNotEmpty() ? gmName : juce::String (p)) + "（音なし）"_ju,
                        row.withTrimmedLeft (10.0f).withTrimmedRight (40.0f), juce::Justification::centredLeft, true);
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (13.5f));
            g.drawText (toJuce (collab::midiNoteName (p)), row.withTrimmedRight (6.0f), juce::Justification::centredRight, false);

            // 仲間（キック・スネア・ハイハット…）の境目
            if (rowIndex + 1 < (int) owner.drumRows.size() && drumFamily (owner.drumRows[(size_t) rowIndex + 1]) != drumFamily (p))
            {
                g.setColour (Theme::background);
                g.fillRect (row.getX(), row.getBottom() - 2.0f, row.getWidth(), 2.0f);
            }

            continue;
        }
        else
        {
            g.setColour (isBlackKey (p) ? juce::Colour (0xff202225) : juce::Colour (0xffd8dadd));
            g.fillRect (row.reduced (0.0f, 0.5f));

            if (p % 12 == 0)
            {
                g.setColour (juce::Colour (0xff303236));
                g.drawText (toJuce (collab::midiNoteName (p)), row.withTrimmedRight (4.0f), juce::Justification::centredRight);
            }
        }
    }

    g.setColour (Theme::background);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void PianoKeyboard::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    owner.handleWheel (e, w, this);
}

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

            if (rowIndex + 1 < (int) owner.drumRows.size() && drumFamily (owner.drumRows[(size_t) rowIndex + 1]) != drumFamily (p))
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
            g.setColour (velocityColour (n.velocity, base).withAlpha (outside || silent ? 0.3f : 1.0f));
            g.fillRoundedRectangle (hit, 3.0f);
            g.setColour (selected ? Theme::selection : juce::Colours::white.withAlpha (0.25f));
            g.drawRoundedRectangle (hit, 3.0f, selected ? 2.0f : 1.0f);

            if (silent)
            {
                g.setColour (Theme::warning);
                g.drawLine (hit.getX(), hit.getBottom(), hit.getRight(), hit.getY(), 1.0f);
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

    if (owner.ctx.state.tool != EditTool::select)
        return setMouseCursor (Theme::toolCursor (owner.ctx.state.tool));

    bool edge = false;
    auto* n = hitNote (e.position, edge);
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

    if (clip == nullptr || owner.selectedNotes.empty())
        return false;

    const auto sel = owner.selectedNotes;
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        // Shift または Ctrl と一緒ならオクターブ
        const bool octave = key.getModifiers().isShiftDown() || key.getModifiers().isCommandDown();
        const int d = (code == juce::KeyPress::upKey ? 1 : -1) * (octave ? 12 : 1);
        owner.editNotes ("ノートの移調"_ju, [sel, d] (collab::MidiClip& c)
        {
            for (auto& n : c.notes)
                if (sel.count (n.id) > 0)
                    n.pitch = juce::jlimit (0, 127, n.pitch + d);
        });
        return true;
    }

    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const auto step = owner.ctx.state.grid.stepTicks() * (code == juce::KeyPress::leftKey ? -1 : 1);
        owner.editNotes ("ノートの移動"_ju, [sel, step] (collab::MidiClip& c)
        {
            for (auto& n : c.notes)
                if (sel.count (n.id) > 0)
                    n.tick = juce::jlimit<collab::Tick> (0, c.lengthTick - 1, n.tick + step);
        });
        return true;
    }

    return false;
}

//==============================================================================
VelocityLane::VelocityLane (PianoRollView& o) : owner (o)
{
    setTooltip ("ベロシティ: ドラッグで変更（選択中のノートがあれば選択中のノートだけ）"_ju);
}

void VelocityLane::paint (juce::Graphics& g)
{
    g.fillAll (Theme::laneAlt);
    auto* clip = owner.getClip();
    auto* track = owner.getTrack();

    g.setColour (Theme::background);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    if (clip == nullptr)
        return;

    const auto& axis = owner.axis();
    const auto base = track != nullptr ? Theme::parseColour (track->color) : Theme::accent;
    const float h = (float) getHeight() - 4.0f;

    for (auto& n : clip->notes)
    {
        const float x = (float) axis.tickToX ((double) (clip->startTick + n.tick));

        if (x < -4 || x > (float) getWidth())
            continue;

        const float bh = h * (float) n.velocity / 127.0f;
        const bool selected = owner.selectedNotes.count (n.id) > 0;
        g.setColour (selected ? Theme::selection : velocityColour (n.velocity, base));
        g.fillRect (juce::Rectangle<float> (x, (float) getHeight() - bh, 3.0f, bh));
        g.fillEllipse (x - 2.0f, (float) getHeight() - bh - 3.0f, 7.0f, 7.0f);
    }
}

void VelocityLane::applyAt (float x1, float x2, float y)
{
    auto* clip = owner.getClip();

    if (clip == nullptr)
        return;

    if (x1 > x2)
        std::swap (x1, x2);

    x1 -= 4.0f;
    x2 += 4.0f;

    const int vel = juce::jlimit (1, 127, (int) std::round ((1.0f - (y - 4.0f) / ((float) getHeight() - 4.0f)) * 127.0f));
    const auto t1 = owner.axis().xToTick (x1) - (double) clip->startTick;
    const auto t2 = owner.axis().xToTick (x2) - (double) clip->startTick;
    const auto sel = owner.selectedNotes;

    owner.lastVelocity = vel;
    owner.editNotes ("ベロシティの変更"_ju, [t1, t2, vel, sel] (collab::MidiClip& c)
    {
        for (auto& n : c.notes)
            if ((double) n.tick >= t1 && (double) n.tick <= t2 && (sel.empty() || sel.count (n.id) > 0))
                n.velocity = vel;
    }, mergeId);
}

void VelocityLane::mouseDown (const juce::MouseEvent& e)
{
    mergeId = juce::Uuid().toString();
    lastX = e.position.x;
    applyAt (lastX, lastX, e.position.y);
}

void VelocityLane::mouseDrag (const juce::MouseEvent& e)
{
    applyAt (lastX, e.position.x, e.position.y);
    lastX = e.position.x;
}

void VelocityLane::mouseUp (const juce::MouseEvent&)
{
    owner.ctx.document.endMerge();
}

//==============================================================================
void AudioClipGrid::paint (juce::Graphics& g)
{
    g.fillAll (Theme::lane);

    const auto& axis = owner.axis();
    const auto& map = owner.ctx.document.getTempoMap();
    auto* clip = owner.getAudioClip();
    auto* track = owner.getTrack();

    if (clip == nullptr || track == nullptr)
        return;

    const float x1 = (float) axis.tickToX ((double) clip->startTick);
    const float x2 = (float) axis.tickToX ((double) collab::audioClipEndTick (*clip, map));
    const auto colour = Theme::parseColour (track->color);
    const auto r = juce::Rectangle<float> (x1, 4.0f, juce::jmax (2.0f, x2 - x1), (float) getHeight() - 8.0f);

    g.setColour (colour.withAlpha (0.18f));
    g.fillRect (r);

    TimeGrid::drawGrid (g, getLocalBounds(), axis, map, &owner.ctx.state.grid);

    // 中心線
    g.setColour (Theme::gridBeat);
    g.drawHorizontalLine (getHeight() / 2, r.getX(), r.getRight());

    if (auto* thumb = owner.ctx.audioCache.getThumbnail (owner.ctx.document.getProjectDir(), clip->audioHash))
    {
        g.setColour (colour.brighter (0.6f));
        const double start = (double) clip->sourceOffsetSamples / collab::kSampleRate;
        const double end = start + (double) clip->lengthSamples / collab::kSampleRate;
        thumb->drawChannels (g, r.reduced (0.0f, 6.0f).toNearestInt(), start, end,
                             juce::Decibels::decibelsToGain ((float) clip->gainDb));
    }
    else
    {
        g.setColour (Theme::warning);
        g.setFont (juce::FontOptions (15.5f));
        g.drawText ("オーディオが見つかりません"_ju, getLocalBounds(), juce::Justification::centred);
    }

    // フェード
    const auto len = (float) juce::jmax<collab::SampleCount> (1, clip->lengthSamples);
    const float fadeInW = r.getWidth() * (float) clip->fadeInSamples / len;
    const float fadeOutW = r.getWidth() * (float) clip->fadeOutSamples / len;
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    juce::Path fades;
    fades.addTriangle (r.getX(), r.getBottom(), r.getX() + fadeInW, r.getY(), r.getX(), r.getY());
    fades.addTriangle (r.getRight(), r.getBottom(), r.getRight() - fadeOutW, r.getY(), r.getRight(), r.getY());
    g.fillPath (fades);

    g.setColour (colour);
    g.drawRect (r, 1.0f);

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (15.0f));
    auto label = toJuce (clip->displayName);
    if (std::abs (clip->gainDb) > 0.05)
        label << "  " << juce::String (clip->gainDb, 1) << " dB";
    g.drawText (label, r.reduced (6.0f, 2.0f).removeFromTop (16.0f), juce::Justification::centredLeft, true);
}

void AudioClipGrid::mouseDown (const juce::MouseEvent& e)
{
    // クリックした位置へ再生位置を動かす（クオンタイズ値にスナップ）
    const double tick = owner.axis().xToTick (e.position.x);
    owner.ctx.engine.setPositionTick (owner.ctx.state.snapCursor (juce::jmax (0.0, tick), owner.ctx.document.getTempoMap(), e.mods));
}

void AudioClipGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    owner.handleWheel (e, w, this);
}

//==============================================================================
PianoRollView::PianoRollView (AppContext& c)
    : ctx (c),
      ruler (c.document, c.state, c.state.pianoRoll),
      playhead (c.state.pianoRoll)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (juce::FontOptions (15.5f, juce::Font::bold));

    int id = 1;
    for (auto& g : collab::Grid::presets())
        gridBox.addItem (toJuce (g.label()), id++);

    gridBox.setTooltip ("クオンタイズ値（スナップ・クオンタイズ・再生位置の単位）"_ju);
    gridBox.onChange = [this]
    {
        auto presets = collab::Grid::presets();
        const int i = gridBox.getSelectedId() - 1;

        if (i >= 0 && i < (int) presets.size())
        {
            lastNoteLength = presets[(size_t) i].stepTicks();
            ctx.state.setQuantise (presets[(size_t) i]);
        }
    };
    addAndMakeVisible (gridBox);

    snapToggle.setToggleState (true, juce::dontSendNotification);
    snapToggle.setTooltip ("クオンタイズ値にスナップ（J で切り替え。Alt を押しながらドラッグで一時的に解除）"_ju);
    snapToggle.onClick = [this] { ctx.state.setSnapEnabled (snapToggle.getToggleState()); };
    addAndMakeVisible (snapToggle);

    quantiseButton.setTooltip ("選択中のノート（選択がなければクリップ内のすべて）の開始位置をグリッドに合わせる"_ju);
    quantiseButton.onClick = [this] { quantiseSelection(); };
    addAndMakeVisible (quantiseButton);

    hintLabel.setText ("鉛筆: クリックでノート追加（ドラッグで長さ）・ノートをクリックで削除　選択: ドラッグで移動・範囲選択、右端で長さ　↑↓: 移調　Del: 削除"_ju,
                       juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, Theme::textDim);
    hintLabel.setFont (juce::FontOptions (15.0f));
    hintLabel.setMinimumHorizontalScale (0.5f);
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (ruler);
    addAndMakeVisible (keyboard);
    addAndMakeVisible (grid);
    addAndMakeVisible (velocity);
    addChildComponent (audioGrid);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);
    addAndMakeVisible (playhead);

    ruler.onSeek = [this] (double tick, const juce::ModifierKeys& mods)
    {
        ctx.engine.setPositionTick (ctx.state.snapCursor (tick, ctx.document.getTempoMap(), mods));
    };
    ruler.onWheel = [this] (auto& e, auto& w) { handleWheel (e, w, &ruler); };

    hScroll.addListener (this);
    vScroll.addListener (this);
    hScroll.setAutoHide (false);
    vScroll.setAutoHide (false);

    scrollY = (127 - 72) * noteHeight;

    ctx.document.addChangeListener (this);
    ctx.state.addChangeListener (this);
    gridBox.setSelectedId (ctx.state.quantisePresetIndex() + 1, juce::dontSendNotification);

    clipChanged();
}

PianoRollView::~PianoRollView()
{
    ctx.document.removeChangeListener (this);
    ctx.state.removeChangeListener (this);
}

const collab::AudioClip* PianoRollView::getAudioClip() const
{
    if (getClip() != nullptr)
        return nullptr;

    if (auto* t = getTrack())
        for (auto& c : t->audioClips)
            if (c.id == ctx.state.selectedClipId)
                return &c;

    return nullptr;
}

void PianoRollView::rebuildDrumRows()
{
    drumRows.clear();

    if (! isDrumTrack())
        return;

    // 叩く頻度の高い順（EZ Drummer の並びに近い）: キック → スネア → ハイハット → タム（高い順）→ シンバル
    const int order[] = { 36, 35, 38, 40, 37, 39, 42, 44, 46, 50, 48, 47, 45, 43, 41, 51, 59, 53, 49, 57, 55, 52 };

    for (int p : order)
        if (drumPieceName (p).isNotEmpty())
            drumRows.push_back (p);

    // キットにあるが上の並びにない音と、キットにないのにノートがある音（消せるように）は後ろに
    for (int p = 0; p < 128; ++p)
    {
        const bool listed = std::find (drumRows.begin(), drumRows.end(), p) != drumRows.end();
        bool used = false;

        if (auto* clip = getClip())
            used = std::any_of (clip->notes.begin(), clip->notes.end(), [p] (auto& n) { return n.pitch == p; });

        if (! listed && (drumPieceName (p).isNotEmpty() || used))
            drumRows.push_back (p);
    }
}

juce::String PianoRollView::drumPieceName (int note) const
{
    auto* t = getTrack();

    if (t == nullptr || ! t->instrument)
        return {};

    if (auto* m = ctx.library.find (t->instrument->id, t->instrument->version))
        for (auto& piece : m->pieces)
            if (piece.note == note)
                return toJuce (piece.displayName);

    return {};
}

bool PianoRollView::isDrumTrack() const
{
    auto* t = getTrack();
    return t != nullptr && t->instrument && t->instrument->kind == collab::Instrument::Kind::builtin
             && t->instrument->id == collab::builtin::drums;
}

void PianoRollView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::background);
    g.fillRect (0, 0, getWidth(), 3);
}

void PianoRollView::resized()
{
    shownAsDrums = isDrumTrack();
    noteHeight = shownAsDrums ? 26 : 14;
    rebuildDrumRows();
    shownAsAudio = getAudioClip() != nullptr;

    for (auto* c : std::initializer_list<juce::Component*> { &keyboard, &grid, &velocity, &vScroll, &snapToggle, &quantiseButton })
        c->setVisible (! shownAsAudio);
    audioGrid.setVisible (shownAsAudio);

    auto area = getLocalBounds().withTrimmedTop (3);
    auto toolbar = area.removeFromTop (toolbarHeight).reduced (6, 3);
    titleLabel.setBounds (toolbar.removeFromLeft (220));
    gridBox.setBounds (toolbar.removeFromLeft (110));
    toolbar.removeFromLeft (6);
    snapToggle.setBounds (toolbar.removeFromLeft (90));
    quantiseButton.setBounds (toolbar.removeFromLeft (100));
    toolbar.removeFromLeft (10);
    hintLabel.setBounds (toolbar);

    if (shownAsAudio)
    {
        area.removeFromRight (scrollBarSize);
        hScroll.setBounds (area.removeFromBottom (scrollBarSize));
        ruler.setBounds (area.removeFromTop (rulerHeight));
        audioGrid.setBounds (area);
        grid.setBounds (area.getX(), area.getY(), area.getWidth(), 0);   // 表示幅の計算（スクロールバー）用
        playhead.setBounds (ruler.getX(), ruler.getY(), ruler.getWidth(), audioGrid.getBottom() - ruler.getY());
        playhead.refresh();
        updateScrollBars();
        return;
    }

    vScroll.setBounds (area.removeFromRight (scrollBarSize).withTrimmedTop (rulerHeight).withTrimmedBottom (velocityHeight + scrollBarSize));

    auto left = area.removeFromLeft (keyboardWidth());
    hScroll.setBounds (area.removeFromBottom (scrollBarSize));
    left.removeFromBottom (scrollBarSize);

    ruler.setBounds (area.removeFromTop (rulerHeight));
    left.removeFromTop (rulerHeight);
    velocity.setBounds (area.removeFromBottom (velocityHeight));
    left.removeFromBottom (velocityHeight);
    grid.setBounds (area);
    keyboard.setBounds (left);

    playhead.setBounds (ruler.getX(), ruler.getY(), ruler.getWidth(), velocity.getBottom() - ruler.getY());
    playhead.refresh();
    updateScrollBars();
}

void PianoRollView::setPlayheadTick (double tick)
{
    playhead.setTick (tick);
}

void PianoRollView::followPlayhead (double tick)
{
    auto& a = axis();
    const double visible = grid.getWidth() / a.pixelsPerTick();

    if (getClip() != nullptr && grid.getWidth() > 0 && (tick < a.scrollTick || tick > a.scrollTick + visible * 0.95))
    {
        a.scrollTick = juce::jmax (0.0, tick - visible * 0.05);
        ctx.state.changed();
    }
}

void PianoRollView::previewNote (int pitch, int vel)
{
    if (auto* t = getTrack())
        ctx.engine.previewNote (t->id, pitch, vel);
}

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

    // 再生位置がクリップの中ならそこ、外ならクリップの先頭に貼る（Cubase と同じく位置は再生位置が基準）
    auto at = (collab::Tick) std::llround (ctx.state.snapCursor (ctx.state.playheadTick, ctx.document.getTempoMap(), {})) - clip->startTick;

    if (at < 0 || at >= clip->lengthTick)
        at = 0;

    auto notes = noteClipboard;
    std::set<std::string> ids;

    for (auto& n : notes)
    {
        n.id = collab::generateUuid();
        n.tick += at;
        ids.insert (n.id);
    }

    editNotes ("ノートの貼り付け"_ju, [notes] (collab::MidiClip& c) { c.notes.insert (c.notes.end(), notes.begin(), notes.end()); });
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

void PianoRollView::handleWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& w, juce::Component*)
{
    auto& ax = axis();

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        ax.zoomAround (e.getEventRelativeTo (&grid).position.x, w.deltaY > 0 ? 1.15 : 1.0 / 1.15, 10.0, 2000.0);
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY) || shownAsAudio)
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        ax.scrollTick = juce::jmax (0.0, ax.scrollTick - d * 400.0 / ax.pixelsPerTick());
    }
    else
    {
        scrollY = juce::jlimit (0, juce::jmax (0, numRows() * noteHeight - grid.getHeight()), scrollY - (int) (w.deltaY * 200.0f));
        updateScrollBars();
        repaint();
        return;
    }

    ctx.state.changed();
}

void PianoRollView::updateScrollBars()
{
    const auto& map = ctx.document.getTempoMap();
    const auto end = (double) map.barToTick (map.tickToBar (ctx.document.getProject().contentEndTick()) + 32);
    const double visible = grid.getWidth() / axis().pixelsPerTick();

    hScroll.setRangeLimits (0.0, juce::jmax (end, axis().scrollTick + visible));
    hScroll.setCurrentRange (axis().scrollTick, visible, juce::dontSendNotification);

    scrollY = juce::jlimit (0, juce::jmax (0, numRows() * noteHeight - grid.getHeight()), scrollY);
    vScroll.setRangeLimits (0.0, (double) (numRows() * noteHeight));
    vScroll.setCurrentRange (scrollY, grid.getHeight(), juce::dontSendNotification);
}

void PianoRollView::scrollBarMoved (juce::ScrollBar* bar, double newStart)
{
    if (bar == &hScroll)
    {
        axis().scrollTick = juce::jmax (0.0, newStart);
        ctx.state.changed();
    }
    else
    {
        scrollY = (int) newStart;
        repaint();
    }
}

void PianoRollView::clipChanged()
{
    auto* clip = getClip();
    auto* audio = getAudioClip();
    shownClipId = clip != nullptr ? clip->id : (audio != nullptr ? audio->id : std::string());
    selectedNotes.clear();

    if (audio != nullptr)
    {
        resized();

        if (audioGrid.getWidth() > 0)
        {
            const auto start = (double) audio->startTick;
            const double len = juce::jmax ((double) collab::kPpq, (double) collab::audioClipEndTick (*audio, ctx.document.getTempoMap()) - start);
            axis().pixelsPerQuarter = juce::jlimit (10.0, 2000.0, audioGrid.getWidth() * 0.9 / (len / collab::kPpq));
            axis().scrollTick = juce::jmax (0.0, start - len * 0.03);
        }

        updateTitle();
        resized();
        repaint();
        return;
    }

    if (clip == nullptr)
    {
        titleLabel.setText ("ピアノロール"_ju, juce::dontSendNotification);
        updateTitle();
        resized();
        repaint();
        return;
    }

    // クリップ全体が見えるように横をあわせ、ノートのある音域へ縦をスクロールする
    if (grid.getWidth() > 0)
    {
        const double len = (double) juce::jmax<collab::Tick> (collab::kPpq * 4, clip->lengthTick);
        axis().pixelsPerQuarter = juce::jlimit (10.0, 400.0, grid.getWidth() * 0.9 / (len / collab::kPpq));
        axis().scrollTick = juce::jmax (0.0, (double) clip->startTick - len * 0.03);
    }

    int centre = isDrumTrack() ? 44 : 60;

    if (! clip->notes.empty())
    {
        int sum = 0;
        for (auto& n : clip->notes)
            sum += n.pitch;
        centre = sum / (int) clip->notes.size();
    }

    resized();   // ドラムかどうか（行の高さ・並び）を先に決める
    scrollY = isDrumTrack() ? 0 : (127 - centre) * noteHeight - grid.getHeight() / 2;
    updateScrollBars();
    repaint();
}

void PianoRollView::updateTitle()
{
    auto* t = getTrack();
    const auto& map = ctx.document.getTempoMap();

    if (auto* clip = getClip(); t != nullptr && clip != nullptr)
        titleLabel.setText (toJuce (t->name) + "  " + juce::String (map.tickToBar (clip->startTick)) + "小節〜"_ju, juce::dontSendNotification);
    else if (auto* audio = getAudioClip(); t != nullptr && audio != nullptr)
        titleLabel.setText (toJuce (t->name) + "  " + juce::String (map.tickToBar (audio->startTick)) + "小節〜（オーディオ）"_ju, juce::dontSendNotification);

    hintLabel.setText (getAudioClip() != nullptr
                         ? "オーディオクリップの拡大表示　クリック: 再生位置　Ctrl+ホイール: ズーム　ホイール: 横スクロール"_ju
                     : isDrumTrack()
                         ? "鉛筆: マスをクリックで叩く（もう一度クリックで消す）　選択: ドラッグで移動（行 = 音色）・範囲選択　下の棒: 強さ　Del: 削除"_ju
                         : "鉛筆: クリックでノート追加（ドラッグで長さ）・ノートをクリックで削除　選択: ドラッグで移動・範囲選択、右端で長さ　↑↓: 移調　Del: 削除"_ju,
                       juce::dontSendNotification);
}

void PianoRollView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto* clip = getClip();
    auto* audio = getAudioClip();
    const auto id = clip != nullptr ? clip->id : (audio != nullptr ? audio->id : std::string());

    if (id != shownClipId)
    {
        clipChanged();
    }
    else if (isDrumTrack() != shownAsDrums)
    {
        resized();   // 鍵盤の幅（ドラム名の表示）を切り替える
    }
    else if (clip != nullptr)
    {
        // 消えたノートを選択から外す
        for (auto it = selectedNotes.begin(); it != selectedNotes.end();)
        {
            const bool exists = std::any_of (clip->notes.begin(), clip->notes.end(), [&] (auto& n) { return n.id == *it; });
            it = exists ? std::next (it) : selectedNotes.erase (it);
        }
    }

    updateTitle();

    snapToggle.setToggleState (ctx.state.snapEnabled(), juce::dontSendNotification);
    gridBox.setSelectedId (ctx.state.quantisePresetIndex() + 1, juce::dontSendNotification);
    updateScrollBars();
    playhead.refresh();
    repaint();
}
