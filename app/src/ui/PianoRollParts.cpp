// ピアノロールの周りの部品: 鍵盤（ドラムは音の名前）、ベロシティの段、オーディオクリップの拡大表示

#include "PianoRoll.h"
#include "PianoRollDetail.h"

#include "TimeGrid.h"
#include "audio/AudioFiles.h"
#include "collab/ClipEditing.h"
#include "collab/GmDrumMap.h"
#include "collab/Uuid.h"

using namespace PianoRollDetail;

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
            if (rowIndex + 1 < (int) owner.drumRows.size() && owner.drumFamilyOf (owner.drumRows[(size_t) rowIndex + 1]) != owner.drumFamilyOf (p))
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
VelocityLane::VelocityLane (PianoRollView& o, bool pitchBendLane) : owner (o), bendMode (pitchBendLane)
{
    setTooltip (bendMode ? "鉛筆ツールでクリックすると点を置きます（時間はグリッド、高さは半音の線に合わせる。Alt で自由に）。点はドラッグで動かし、ダブルクリックで消します"_ju
                         : "ノートを選んでから、上下にドラッグでベロシティを変えます（選んだノートはまとめて）"_ju);
}

void VelocityLane::resized() {}

void VelocityLane::paint (juce::Graphics& g)
{
    g.fillAll (Theme::laneAlt);
    auto* clip = owner.getClip();
    auto* track = owner.getTrack();

    g.setColour (Theme::background);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    if (clip == nullptr)
        return;

    if (bendMode)
        return paintPitchBend (g, *clip);

    const auto& axis = owner.axis();
    const auto base = track != nullptr ? Theme::parseColour (track->color) : Theme::accent;
    const float h = (float) getHeight() - 4.0f;

    std::vector<const collab::Note*> notes;

    for (auto& n : clip->notes)
        notes.push_back (&n);

    std::sort (notes.begin(), notes.end(), [] (auto* a, auto* b) { return a->tick < b->tick; });

    for (auto* n : notes)
    {
        const float x = (float) axis.tickToX ((double) (clip->startTick + n->tick));

        if (x < -4 || x > (float) getWidth())
            continue;

        const float bh = h * (float) n->velocity / 127.0f;
        const bool selected = owner.selectedNotes.count (n->id) > 0;
        g.setColour (selected ? Theme::selection : velocityColour (n->velocity, base));
        g.fillRect (juce::Rectangle<float> (x, (float) getHeight() - bh, 3.0f, bh));
        g.fillEllipse (x - 2.0f, (float) getHeight() - bh - 3.0f, 7.0f, 7.0f);
    }

    // 数値: 選んでいるノートと、隣と重ならないノートに出す（同じ位置のノートは 1 つだけ）
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    float lastRight = -1000.0f;

    for (auto* n : notes)
    {
        const float x = (float) axis.tickToX ((double) (clip->startTick + n->tick));
        const bool selected = owner.selectedNotes.count (n->id) > 0;
        const float left = juce::jmax (0.0f, x - 9.0f);

        if (x < -4 || x > (float) getWidth() || (left < lastRight && ! selected))
            continue;

        const float bh = h * (float) n->velocity / 127.0f;
        const float top = juce::jmax (1.0f, (float) getHeight() - bh - 16.0f);
        g.setColour (selected ? Theme::selection : Theme::textDim);
        g.drawText (juce::String (n->velocity), juce::Rectangle<float> (left + 1.5f, top, 20.0f, 12.0f), juce::Justification::centredLeft, false);
        lastRight = left + 22.0f;
    }
}

const collab::Note* VelocityLane::noteAt (float x) const
{
    auto* clip = owner.getClip();

    if (clip == nullptr)
        return nullptr;

    // 棒の上をぴったり押さなくてよいように、左右 7 ピクセルまで拾う（いちばん近い棒）
    const collab::Note* best = nullptr;
    float bestDistance = 7.0f;

    for (auto& n : clip->notes)
    {
        const float d = std::abs ((float) owner.axis().tickToX ((double) (clip->startTick + n.tick)) - x);

        if (d <= bestDistance)
        {
            best = &n;
            bestDistance = d;
        }
    }

    return best;
}

//==============================================================================
float VelocityLane::bendFullScale() const
{
    // 内蔵音源のベンド幅は ±2 半音（値 ±8192）。段の上下の端は ±半音（4096）か ±1 音（8192）
    return 4096.0f * (float) juce::jlimit (1, 2, owner.ctx.state.pitchBendViewSemitones);
}

float VelocityLane::bendY (int value) const
{
    const float mid = (float) getHeight() * 0.5f, half = (float) getHeight() * 0.5f - 8.0f;
    return mid - half * juce::jlimit (-1.1f, 1.1f, (float) value / bendFullScale());
}

int VelocityLane::bendValueAt (float y, bool free) const
{
    const float mid = (float) getHeight() * 0.5f, half = (float) getHeight() * 0.5f - 8.0f;
    const int value = juce::jlimit (collab::kPitchBendMin, collab::kPitchBendMax,
                                    juce::roundToInt (juce::jlimit (-1.0f, 1.0f, (mid - y) / half) * bendFullScale()));

    if (free)
        return value;

    // 半音・四分音の線の近く（7 ピクセル）なら、その線に合わせる（Alt で自由に）
    const int range = owner.ctx.state.pitchBendViewSemitones;

    for (int quarter = -4 * range; quarter <= 4 * range; ++quarter)
        if (std::abs (bendY (quarter * 1024) - y) <= 7.0f)
            return juce::jlimit (collab::kPitchBendMin, collab::kPitchBendMax, quarter * 1024);

    return value;
}

std::optional<juce::Point<float>> VelocityLane::curveHandle (const collab::MidiClip& clip, size_t i) const
{
    if (i + 1 >= clip.pitchBends.size())
        return std::nullopt;

    const auto& a = clip.pitchBends[i];
    const auto& b = clip.pitchBends[i + 1];

    if (a.value == b.value || b.tick - a.tick < 2)
        return std::nullopt;

    const auto mid = a.tick + (b.tick - a.tick) / 2;
    const float x = (float) owner.axis().tickToX ((double) (clip.startTick + mid));
    const float x0 = (float) owner.axis().tickToX ((double) (clip.startTick + a.tick));
    const float x1 = (float) owner.axis().tickToX ((double) (clip.startTick + b.tick));

    if (x1 - x0 < 18.0f)   // 点と重なるほど狭いときは出さない
        return std::nullopt;

    return juce::Point<float> (x, bendY (collab::pitchBendAt (clip.pitchBends, mid)));
}

int VelocityLane::curveHandleAt (const collab::MidiClip& clip, juce::Point<float> p) const
{
    for (size_t i = 0; i + 1 < clip.pitchBends.size(); ++i)
        if (auto h = curveHandle (clip, i); h && h->getDistanceFrom (p) < 7.0f)
            return (int) i;

    return -1;
}

int VelocityLane::bendPointAt (const collab::MidiClip& clip, juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = 8.0f;

    for (size_t i = 0; i < clip.pitchBends.size(); ++i)
    {
        const auto& b = clip.pitchBends[i];
        const juce::Point<float> q ((float) owner.axis().tickToX ((double) (clip.startTick + b.tick)), bendY (b.value));

        if (const float d = q.getDistanceFrom (p); d < bestDistance)
        {
            bestDistance = d;
            best = (int) i;
        }
    }

    return best;
}

void VelocityLane::setBends (std::vector<collab::PitchBend> bends, const juce::String& description)
{
    std::stable_sort (bends.begin(), bends.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });
    owner.editNotes (description, [bends] (collab::MidiClip& c) { c.pitchBends = bends; }, mergeId);
}

void VelocityLane::paintPitchBend (juce::Graphics& g, const collab::MidiClip& clip)
{
    const auto& axis = owner.axis();

    // 半音の線（濃い・数字つき）と四分音の線（薄い）
    const int range = owner.ctx.state.pitchBendViewSemitones;

    for (int quarter = -4 * range; quarter <= 4 * range; ++quarter)
    {
        const float y = bendY (quarter * 1024);
        g.setColour (Theme::overlay (quarter == 0 ? 0.34f : quarter % 4 == 0 ? 0.2f : 0.07f));
        g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());

        if (quarter % 4 == 0)
        {
            const int st = quarter / 4;
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (st > 0 ? "+" + juce::String (st) : juce::String (st), juce::Rectangle<float> (3.0f, y - (st < 0 ? 13.0f : st > 0 ? 0.0f : 6.5f), 22.0f, 12.0f),
                        juce::Justification::centredLeft);
        }
    }

    // クリップの範囲: 点の間は直線
    const float x0 = (float) axis.tickToX ((double) clip.startTick);
    const float x1 = (float) axis.tickToX ((double) clip.endTick());
    auto* track = owner.getTrack();
    const auto colour = track != nullptr ? Theme::parseColour (track->color) : Theme::accent;
    const float mid = bendY (0);

    juce::Path line, area;
    bool started = false;

    for (float x = juce::jmax (0.0f, x0); x <= juce::jmin ((float) getWidth(), x1) + 2.0f; x += 2.0f)
    {
        const auto tick = (collab::Tick) std::llround (axis.xToTick (juce::jmin (x, x1))) - clip.startTick;
        const float y = bendY (collab::pitchBendAt (clip.pitchBends, tick));

        if (! started)
        {
            line.startNewSubPath (x, y);
            area.startNewSubPath (x, mid);
            area.lineTo (x, y);
            started = true;
        }
        else
        {
            line.lineTo (x, y);
            area.lineTo (x, y);
        }
    }

    if (started)
    {
        area.lineTo (juce::jmin ((float) getWidth(), x1), mid);
        area.closeSubPath();
        g.setColour (colour.withAlpha (0.25f));
        g.fillPath (area);
        g.setColour (colour.darker (0.2f));
        g.strokePath (line, juce::PathStrokeType (1.8f));
    }

    // 点（マウスの下・動かしている点は明るく、値を半音で出す）
    const auto at = bendIndex >= 0 ? bendLast : bendHover;
    int labelIndex = -1;

    for (size_t i = 0; i < clip.pitchBends.size(); ++i)
    {
        const auto& b = clip.pitchBends[i];
        const juce::Point<float> c ((float) axis.tickToX ((double) (clip.startTick + b.tick)), bendY (b.value));
        const bool hot = c.getDistanceFrom (at) < 9.0f;
        g.setColour (hot ? Theme::selection : colour.brighter (0.4f));
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c));
        g.setColour (Theme::background);
        g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c), 1.0f);

        if (hot)
            labelIndex = (int) i;
    }

    // 点と点の間のつまみ（上下にドラッグでカーブ。ダブルクリックで直線に戻す）
    for (size_t i = 0; i + 1 < clip.pitchBends.size(); ++i)
        if (auto h = curveHandle (clip, i))
        {
            const bool hot = (int) i == curveIndex || h->getDistanceFrom (at) < 7.0f;
            auto box = juce::Rectangle<float> (hot ? 8.0f : 6.0f, hot ? 8.0f : 6.0f).withCentre (*h);
            g.setColour (hot ? Theme::selection : Theme::background.withAlpha (0.9f));
            g.fillRect (box);
            g.setColour (hot ? Theme::background : colour.brighter (0.3f));
            g.drawRect (box, 1.2f);
        }

    // 鉛筆: クリックで置かれる点（時間はグリッド、高さは半音の線）を薄く出す
    if (const auto ghost = ghostBend (clip); ghost && labelIndex < 0 && bendIndex < 0 && curveIndex < 0)
    {
        const juce::Point<float> c ((float) axis.tickToX ((double) (clip.startTick + ghost->tick)), bendY (ghost->value));
        g.setColour (Theme::selection.withAlpha (0.35f));
        g.drawVerticalLine ((int) c.x, 0.0f, (float) getHeight());
        g.setColour (Theme::selection.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (c));
        g.setColour (Theme::selection);
        g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (c), 1.2f);

        const double semis = (double) ghost->value / 4096.0;
        const auto text = (semis > 0 ? "+" : "") + juce::String (semis, 2);
        auto box = juce::Rectangle<float> (46.0f, 16.0f).withPosition (c.x + 7.0f, juce::jlimit (0.0f, (float) getHeight() - 16.0f, c.y - 18.0f));
        g.setColour (Theme::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (text, box, juce::Justification::centred);
    }

    if (labelIndex >= 0)
    {
        const auto& b = clip.pitchBends[(size_t) labelIndex];
        const float x = (float) axis.tickToX ((double) (clip.startTick + b.tick));
        const double semis = (double) b.value / 4096.0;
        const auto text = (semis > 0 ? "+" : "") + juce::String (semis, 2);
        auto box = juce::Rectangle<float> (46.0f, 16.0f).withPosition (x + 7.0f, juce::jlimit (0.0f, (float) getHeight() - 16.0f, bendY (b.value) - 18.0f));
        g.setColour (Theme::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (text, box, juce::Justification::centred);
    }
}

std::optional<collab::PitchBend> VelocityLane::ghostBend (const collab::MidiClip& clip) const
{
    if (! owner.ctx.state.pencil() || ! getLocalBounds().toFloat().contains (bendHover) || bendPointAt (clip, bendHover) >= 0
         || curveHandleAt (clip, bendHover) >= 0)
        return std::nullopt;

    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    const auto tick = owner.snap (owner.axis().xToTick (bendHover.x), false, mods) - clip.startTick;

    if (tick < 0 || tick >= clip.lengthTick)
        return std::nullopt;

    return collab::PitchBend { tick, bendValueAt (bendHover.y, mods.isAltDown()) };
}

void VelocityLane::mouseExit (const juce::MouseEvent&)
{
    if (bendMode)
    {
        bendHover = { -100.0f, -100.0f };
        repaint();
    }
}

void VelocityLane::mouseDoubleClick (const juce::MouseEvent&)
{
    // ピッチベンドの点はダブルクリックで消す（mouseDown で扱う）
}

void VelocityLane::mouseMove (const juce::MouseEvent& e)
{
    if (bendMode)
    {
        bendHover = e.position;
        repaint();
        auto* clip = owner.getClip();
        const bool onPoint = clip != nullptr && bendPointAt (*clip, e.position) >= 0;
        const bool onHandle = clip != nullptr && ! onPoint && curveHandleAt (*clip, e.position) >= 0;
        return setMouseCursor (onPoint ? juce::MouseCursor::DraggingHandCursor
                               : onHandle ? juce::MouseCursor::UpDownResizeCursor
                               : owner.ctx.state.pencil() ? Theme::pencilCursor() : juce::MouseCursor::NormalCursor);
    }

    setMouseCursor (owner.selectedNotes.empty() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
}

void VelocityLane::mouseDown (const juce::MouseEvent& e)
{
    if (bendMode)
    {
        auto* clip = owner.getClip();

        if (clip == nullptr)
            return;

        mergeId = juce::Uuid().toString();
        originalBends = clip->pitchBends;
        bendLast = e.position;
        bendDownTick = owner.axis().xToTick (e.position.x);
        bendIndex = -1;
        curveIndex = -1;
        const int index = bendPointAt (*clip, e.position);
        const int handle = index < 0 ? curveHandleAt (*clip, e.position) : -1;
        auto bends = clip->pitchBends;

        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem ("この点を消す"_ju, index >= 0, false, [this, bends, index]() mutable
            {
                bends.erase (bends.begin() + index);
                mergeId = {};
                setBends (bends, "ピッチベンドの点を削除"_ju);
            });
            m.addItem ("ピッチベンドを全部消す"_ju, ! bends.empty(), false, [this]
            {
                mergeId = {};
                setBends ({}, "ピッチベンドを全部消す"_ju);
            });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }

        // 点をダブルクリックで消す。カーブのつまみをダブルクリックで直線に戻す
        if (e.getNumberOfClicks() > 1)
        {
            if (index >= 0)
            {
                bends.erase (bends.begin() + index);
                setBends (bends, "ピッチベンドの点を削除"_ju);
            }
            else if (handle >= 0)
            {
                bends[(size_t) handle].curve = 0.0;
                setBends (bends, "ピッチベンドのカーブを直線に"_ju);
            }

            return;
        }

        if (index >= 0)
        {
            bendIndex = index;   // 点を動かす
            return;
        }

        if (handle >= 0)
        {
            curveIndex = handle;   // カーブを変える（どのツールでも）
            return;
        }

        // 点を置くのは鉛筆ツールのとき（選択ツールでは点を動かす・消すだけ）
        if (! owner.ctx.state.pencil())
            return;

        // 押した所（時間はグリッド、高さは半音の線の近くなら線）に点を置き、そのままドラッグで動かせる
        const auto tick = juce::jlimit<collab::Tick> (0, std::max<collab::Tick> (0, clip->lengthTick - 1),
                                                      owner.snap (owner.axis().xToTick (e.position.x), false, e.mods) - clip->startTick);
        const collab::PitchBend added { tick, bendValueAt (e.position.y, e.mods.isAltDown()) };

        // 同じ時刻に置けるのは 2 つまで（1 つ目から 2 つ目へ、その時刻で跳ぶ。しゃくりの始まりなど）。3 つ目は 2 つ目を置き換える
        if (std::count_if (bends.begin(), bends.end(), [&] (auto& b) { return b.tick == added.tick; }) >= 2)
        {
            auto last = std::find_if (bends.rbegin(), bends.rend(), [&] (auto& b) { return b.tick == added.tick; });
            bends.erase (std::next (last).base());
        }

        bends.push_back (added);
        std::stable_sort (bends.begin(), bends.end(), [] (auto& a2, auto& b) { return a2.tick < b.tick; });
        setBends (bends, "ピッチベンドの点を追加"_ju);
        originalBends = bends;
        bendIndex = -1;

        for (size_t i = bends.size(); i-- > 0;)
            if (bends[i].tick == added.tick && bends[i].value == added.value)
            {
                bendIndex = (int) i;
                break;
            }

        return;
    }

    // 棒を押したら、そのノートを選ぶ（値は変えない。Shift で選択に足す）
    if (auto* n = noteAt (e.position.x); n != nullptr && owner.selectedNotes.count (n->id) == 0)
    {
        if (! e.mods.isShiftDown())
            owner.selectedNotes.clear();

        owner.selectedNotes.insert (n->id);
        owner.repaint();
    }

    // 選んでいるノートを、ドラッグした分だけ上下させる（押した位置には合わせない）
    mergeId = juce::Uuid().toString();
    downY = e.position.y;
    originalVelocities.clear();

    if (auto* clip = owner.getClip())
        for (auto& n : clip->notes)
            if (owner.selectedNotes.count (n.id) > 0)
                originalVelocities[n.id] = n.velocity;
}

void VelocityLane::mouseDrag (const juce::MouseEvent& e)
{
    if (bendMode)
    {
        auto* clip = owner.getClip();

        if (clip == nullptr)
            return;

        // カーブ: つまみの高さ（真ん中の時刻での値）から決める
        if (curveIndex >= 0 && curveIndex + 1 < (int) originalBends.size())
        {
            bendLast = e.position;
            auto bends = originalBends;
            auto& a = bends[(size_t) curveIndex];
            const auto& b = bends[(size_t) curveIndex + 1];
            const double target = bendValueAt (juce::jlimit (0.0f, (float) getHeight(), e.position.y), true);
            a.curve = std::round (collab::pitchBendCurveFor ((target - a.value) / (double) (b.value - a.value)) * 100.0) / 100.0;
            setBends (bends, "ピッチベンドのカーブ"_ju);
            return;
        }

        if (bendIndex < 0 || bendIndex >= (int) originalBends.size())
            return;

        // 点を動かす（時間はグリッド、Alt で自由に。高さは半音・四分音の線に吸い付く）
        bendLast = e.position;
        auto bends = originalBends;
        auto& moved = bends[(size_t) bendIndex];
        const auto absolute = (double) (clip->startTick + moved.tick) + (owner.axis().xToTick (e.position.x) - bendDownTick);
        moved.tick = juce::jlimit<collab::Tick> (0, std::max<collab::Tick> (0, clip->lengthTick - 1), owner.snap (absolute, false, e.mods) - clip->startTick);
        moved.value = bendValueAt (juce::jlimit (0.0f, (float) getHeight(), e.position.y), e.mods.isAltDown());

        // 同じ時刻は 2 つまで（ほかに 2 つあれば、動かした点に近い方を外す）
        std::vector<size_t> same;

        for (size_t i = 0; i < bends.size(); ++i)
            if (i != (size_t) bendIndex && bends[i].tick == moved.tick)
                same.push_back (i);

        if (same.size() >= 2)
            bends.erase (bends.begin() + (std::ptrdiff_t) same.back());

        setBends (bends, "ピッチベンドの点を移動"_ju);   // 並べ直しても、同じ時刻の点どうしの前後はそのまま
        return;
    }

    if (originalVelocities.empty())
        return;

    const int delta = (int) std::round ((downY - e.position.y) / ((float) getHeight() - 4.0f) * 127.0f);
    const auto orig = originalVelocities;

    owner.editNotes ("ベロシティの変更"_ju, [orig, delta] (collab::MidiClip& c)
    {
        for (auto& n : c.notes)
            if (auto it = orig.find (n.id); it != orig.end())
                n.velocity = juce::jlimit (1, 127, it->second + delta);
    }, mergeId);

    if (originalVelocities.size() == 1)
        if (auto* clip = owner.getClip())
            for (auto& n : clip->notes)
                if (n.id == originalVelocities.begin()->first)
                    owner.lastVelocity = n.velocity;
}

void VelocityLane::mouseUp (const juce::MouseEvent&)
{
    bendIndex = -1;
    curveIndex = -1;
    repaint();
    originalVelocities.clear();
    owner.ctx.document.endMerge();
}

