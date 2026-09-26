#include "PianoRoll.h"

#include "TimeGrid.h"
#include "collab/GmDrumMap.h"
#include "collab/Uuid.h"

namespace
{
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
    g.setFont (juce::FontOptions (11.0f));

    for (int p = 0; p < 128; ++p)
    {
        const float y = owner.pitchToY (p);

        if (y + (float) owner.noteHeight < 0 || y > (float) getHeight())
            continue;

        const auto row = juce::Rectangle<float> (0.0f, y, (float) getWidth(), (float) owner.noteHeight);

        if (drums)
        {
            const auto name = toJuce (collab::gmDrumName (p));
            g.setColour (name.isNotEmpty() ? Theme::panelLight : Theme::panel);
            g.fillRect (row.reduced (0.0f, 0.5f));
            g.setColour (name.isNotEmpty() ? Theme::text : Theme::textDim);
            g.drawText (name.isNotEmpty() ? juce::String (p) + " " + name : juce::String (p),
                        row.withTrimmedLeft (4.0f), juce::Justification::centredLeft, true);
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
        const float x2 = (float) axis.tickToX ((double) (clip->startTick + it->endTick()));
        const float y = owner.pitchToY (it->pitch);

        if (p.x >= x1 && p.x <= juce::jmax (x2, x1 + 3.0f) && p.y >= y && p.y < y + (float) owner.noteHeight)
        {
            nearRightEdge = x2 - p.x <= edgeGrab && x2 - x1 > edgeGrab * 1.5f;
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

        const bool shaded = drums ? collab::gmDrumName (p).empty() : isBlackKey (p);
        g.setColour (shaded ? Theme::laneAlt : Theme::lane);
        g.fillRect (0.0f, y, (float) getWidth(), (float) owner.noteHeight);

        if (p % 12 == 0 || (drums && p % 12 == 11))
        {
            g.setColour (Theme::gridBeat);
            g.drawHorizontalLine ((int) (y + (float) owner.noteHeight) - 1, 0.0f, (float) getWidth());
        }
    }

    TimeGrid::drawGrid (g, getLocalBounds(), axis, map, &owner.ctx.state.grid);

    if (clip == nullptr)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("タイムラインで MIDI クリップを選択すると、ここで編集できます"_ju, getLocalBounds(), juce::Justification::centred);
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

        const auto r = juce::Rectangle<float> (x1, y + 1.0f, juce::jmax (3.0f, x2 - x1), (float) owner.noteHeight - 2.0f);
        const bool selected = owner.selectedNotes.count (n.id) > 0;
        const bool outside = n.tick >= clip->lengthTick;

        g.setColour (velocityColour (n.velocity, base).withAlpha (outside ? 0.3f : 1.0f));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (selected ? Theme::selection : juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 2.0f, selected ? 2.0f : 1.0f);
    }

    if (mode == Mode::rubberBand)
    {
        g.setColour (Theme::accent.withAlpha (0.2f));
        g.fillRect (rubberBand);
        g.setColour (Theme::accent);
        g.drawRect (rubberBand, 1.0f);
    }
}

void NoteGrid::mouseMove (const juce::MouseEvent& e)
{
    if (owner.ctx.state.pencil())
        return setMouseCursor (Theme::pencilCursor());

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

    if (clip == nullptr)
        return;

    bool edge = false;
    auto* n = hitNote (e.position, edge);
    mergeId = juce::Uuid().toString();

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
        int dp = owner.yToPitch (e.position.y) - downPitch;

        // 全ノートがクリップ・音域の範囲に収まるように制限する
        for (auto& [id, o] : origs)
        {
            dt = juce::jlimit (-o.tick, clipLength - 1 - o.tick, dt);
            dp = juce::jlimit (-o.pitch, 127 - o.pitch, dp);
        }

        owner.editNotes ("ノートの移動"_ju, [origs, dt, dp] (collab::MidiClip& c)
        {
            for (auto& n : c.notes)
                if (auto it = origs.find (n.id); it != origs.end())
                {
                    n.tick = it->second.tick + dt;
                    n.pitch = it->second.pitch + dp;
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
        const int d = (code == juce::KeyPress::upKey ? 1 : -1) * (key.getModifiers().isShiftDown() ? 12 : 1);
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
PianoRollView::PianoRollView (AppContext& c)
    : ctx (c),
      ruler (c.document, c.state, c.state.pianoRoll),
      playhead (c.state.pianoRoll)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));

    int id = 1;
    for (auto& g : collab::Grid::presets())
        gridBox.addItem (toJuce (g.label()), id++);

    gridBox.setTooltip ("グリッド（スナップ・クオンタイズの単位）"_ju);
    gridBox.onChange = [this]
    {
        auto presets = collab::Grid::presets();
        const int i = gridBox.getSelectedId() - 1;

        if (i >= 0 && i < (int) presets.size())
        {
            const bool enabled = ctx.state.grid.enabled;
            ctx.state.grid = presets[(size_t) i];
            ctx.state.grid.enabled = enabled;
            lastNoteLength = ctx.state.grid.stepTicks();
            ctx.state.changed();
        }
    };
    addAndMakeVisible (gridBox);

    snapToggle.setToggleState (true, juce::dontSendNotification);
    snapToggle.setTooltip ("グリッドにスナップ（Alt を押しながらドラッグで一時的に解除）"_ju);
    snapToggle.onClick = [this]
    {
        ctx.state.grid.enabled = snapToggle.getToggleState();
        ctx.state.changed();
    };
    addAndMakeVisible (snapToggle);

    quantiseButton.setTooltip ("選択中のノート（選択がなければクリップ内のすべて）の開始位置をグリッドに合わせる"_ju);
    quantiseButton.onClick = [this] { quantiseSelection(); };
    addAndMakeVisible (quantiseButton);

    hintLabel.setText ("鉛筆: クリックでノート追加（ドラッグで長さ）・ノートをクリックで削除　選択: ドラッグで移動・範囲選択、右端で長さ　↑↓: 移調　Del: 削除"_ju,
                       juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, Theme::textDim);
    hintLabel.setFont (juce::FontOptions (12.0f));
    hintLabel.setMinimumHorizontalScale (0.5f);
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (ruler);
    addAndMakeVisible (keyboard);
    addAndMakeVisible (grid);
    addAndMakeVisible (velocity);
    addAndMakeVisible (hScroll);
    addAndMakeVisible (vScroll);
    addAndMakeVisible (playhead);

    ruler.onSeek = [this] (double tick) { ctx.engine.setPositionTick (tick); };
    ruler.onWheel = [this] (auto& e, auto& w) { handleWheel (e, w, &ruler); };

    hScroll.addListener (this);
    vScroll.addListener (this);
    hScroll.setAutoHide (false);
    vScroll.setAutoHide (false);

    scrollY = (127 - 72) * noteHeight;

    ctx.document.addChangeListener (this);
    ctx.state.addChangeListener (this);

    for (int i = 0; i < (int) collab::Grid::presets().size(); ++i)
        if (collab::Grid::presets()[(size_t) i].stepTicks() == ctx.state.grid.stepTicks())
            gridBox.setSelectedId (i + 1, juce::dontSendNotification);

    clipChanged();
}

PianoRollView::~PianoRollView()
{
    ctx.document.removeChangeListener (this);
    ctx.state.removeChangeListener (this);
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

    auto area = getLocalBounds().withTrimmedTop (3);
    auto toolbar = area.removeFromTop (toolbarHeight).reduced (6, 3);
    titleLabel.setBounds (toolbar.removeFromLeft (220));
    gridBox.setBounds (toolbar.removeFromLeft (110));
    toolbar.removeFromLeft (6);
    snapToggle.setBounds (toolbar.removeFromLeft (90));
    quantiseButton.setBounds (toolbar.removeFromLeft (100));
    toolbar.removeFromLeft (10);
    hintLabel.setBounds (toolbar);

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

void PianoRollView::previewNote (int pitch, int velocity)
{
    if (auto* t = getTrack())
        ctx.engine.previewNote (t->id, pitch, velocity);
}

void PianoRollView::focusEditor()
{
    grid.grabKeyboardFocus();
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
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        ax.scrollTick = juce::jmax (0.0, ax.scrollTick - d * 400.0 / ax.pixelsPerTick());
    }
    else
    {
        scrollY = juce::jlimit (0, juce::jmax (0, 128 * noteHeight - grid.getHeight()), scrollY - (int) (w.deltaY * 200.0f));
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

    scrollY = juce::jlimit (0, juce::jmax (0, 128 * noteHeight - grid.getHeight()), scrollY);
    vScroll.setRangeLimits (0.0, 128.0 * noteHeight);
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
    shownClipId = clip != nullptr ? clip->id : std::string();
    selectedNotes.clear();

    if (clip == nullptr)
    {
        titleLabel.setText ("ピアノロール"_ju, juce::dontSendNotification);
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

    scrollY = (127 - centre) * noteHeight - grid.getHeight() / 2;
    resized();
    repaint();
}

void PianoRollView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto* clip = getClip();
    const auto id = clip != nullptr ? clip->id : std::string();

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

    if (auto* t = getTrack(); t != nullptr && clip != nullptr)
    {
        const auto& map = ctx.document.getTempoMap();
        titleLabel.setText (toJuce (t->name) + "  " + juce::String (map.tickToBar (clip->startTick)) + "小節〜"_ju,
                            juce::dontSendNotification);
    }

    snapToggle.setToggleState (ctx.state.grid.enabled, juce::dontSendNotification);
    updateScrollBars();
    playhead.refresh();
    repaint();
}
