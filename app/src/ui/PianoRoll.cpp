#include "PianoRoll.h"
#include "PianoRollDetail.h"

#include "TimeGrid.h"
#include "audio/AudioFiles.h"
#include "collab/ClipEditing.h"
#include "collab/GmDrumMap.h"
#include "collab/Uuid.h"

using namespace PianoRollDetail;

//==============================================================================
PianoRollView::PianoRollView (AppContext& c)
    : ctx (c),
      ruler (c.document, c.state, c.state.pianoRoll),
      playhead (c.state.pianoRoll)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (juce::FontOptions (15.5f, juce::Font::bold));

    TimeGrid::fillQuantiseBox (gridBox);

    gridBox.setTooltip ("クオンタイズ"_ju);
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
    snapToggle.setTooltip ("スナップ（J）"_ju);
    snapToggle.onClick = [this] { ctx.state.setSnapEnabled (snapToggle.getToggleState()); };
    addAndMakeVisible (snapToggle);

    quantiseButton.setTooltip ("クオンタイズ（Q）"_ju);
    quantiseButton.onClick = [this] { quantiseSelection(); };
    addAndMakeVisible (quantiseButton);


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

    // 叩く頻度の高い順: キック → スネア → ハイハット → タム（高い順）→ シンバル（最後に上下を逆にする）
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

    // 画面では下からキック → スネア → ハイハット → タム → シンバル（EZ Drummer と同じく、キックがいちばん下）
    std::reverse (drumRows.begin(), drumRows.end());
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

void PianoRollView::setTopStrip (TopStrip* strip)
{
    if (topStrip != nullptr)
        removeChildComponent (topStrip);

    topStrip = strip;

    if (topStrip != nullptr)
        addAndMakeVisible (topStrip);

    resized();
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

    if (topStrip != nullptr)
        topStrip->setVisible (! shownAsAudio);

    auto area = getLocalBounds().withTrimmedTop (3);
    auto toolbar = area.removeFromTop (toolbarHeight).reduced (6, 3);
    titleLabel.setBounds (toolbar.removeFromLeft (220));
    gridBox.setBounds (toolbar.removeFromLeft (110));
    toolbar.removeFromLeft (6);
    snapToggle.setBounds (toolbar.removeFromLeft (90));
    quantiseButton.setBounds (toolbar.removeFromLeft (100));
    toolbar.removeFromLeft (10);

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

    const int stripHeight = topStrip != nullptr ? topStrip->preferredHeight() : 0;
    vScroll.setBounds (area.removeFromRight (scrollBarSize).withTrimmedTop (rulerHeight + stripHeight).withTrimmedBottom (velocityHeight + scrollBarSize));

    auto left = area.removeFromLeft (keyboardWidth());
    hScroll.setBounds (area.removeFromBottom (scrollBarSize));
    left.removeFromBottom (scrollBarSize);

    ruler.setBounds (area.removeFromTop (rulerHeight));
    left.removeFromTop (rulerHeight);

    if (topStrip != nullptr)
    {
        const auto row = area.removeFromTop (stripHeight);
        left.removeFromTop (stripHeight);
        topStrip->setLeftWidth (left.getWidth());
        topStrip->setBounds (left.getX(), row.getY(), row.getRight() - left.getX(), stripHeight);
    }
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
    // クリップを選んでいなくても（トラックだけ・オーディオの表示でも）再生位置を追う
    auto& a = axis();
    const int width = shownAsAudio ? audioGrid.getWidth() : grid.getWidth();
    const double visible = width / a.pixelsPerTick();

    if (isShowing() && width > 0 && (tick < a.scrollTick || tick > a.scrollTick + visible * 0.95))
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

        if (ctx.engine.isPlaying())
            ctx.state.autoScroll = false;   // 手で動かしたら自動スクロールをやめる
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

        if (ctx.engine.isPlaying())
            ctx.state.autoScroll = false;   // 手で動かしたら自動スクロールをやめる

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
    // ドラムは下（キック）が見えるように、いちばん下までスクロールしておく
    scrollY = isDrumTrack() ? juce::jmax (0, numRows() * noteHeight - grid.getHeight()) : (127 - centre) * noteHeight - grid.getHeight() / 2;
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
