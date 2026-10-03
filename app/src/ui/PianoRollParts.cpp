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
VelocityLane::VelocityLane (PianoRollView& o) : owner (o)
{
    setTooltip ("ノートを選んでから、上下にドラッグでベロシティを変えます（選んだノートはまとめて）"_ju);
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

void VelocityLane::mouseMove (const juce::MouseEvent&)
{
    setMouseCursor (owner.selectedNotes.empty() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
}

void VelocityLane::mouseDown (const juce::MouseEvent& e)
{
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
    originalVelocities.clear();
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

    TimeGrid::drawGrid (g, getLocalBounds(), axis, map, &owner.ctx.state.grid);

    // クリップは不透明（タイムラインと同じ見た目）
    g.setColour (Theme::clipBody (colour));
    g.fillRect (r);

    // 中心線
    g.setColour (Theme::gridBeat);
    g.drawHorizontalLine (getHeight() / 2, r.getX(), r.getRight());

    if (auto* thumb = owner.ctx.audioCache.getThumbnail (owner.ctx.document.getProjectDir(), clip->audioHash))
    {
        const double start = (double) clip->sourceOffsetSamples / collab::kSampleRate;
        const double end = start + (double) clip->lengthSamples / collab::kSampleRate;
        AudioFiles::drawWaveform (g, *thumb, r.reduced (0.0f, 6.0f), start, end,
                                  juce::Decibels::decibelsToGain ((float) clip->gainDb) * owner.ctx.state.waveformZoom, Theme::clipWave (colour));
        AudioFiles::drawTransients (g, owner.ctx.audioCache.getTransients (owner.ctx.document.getProjectDir(), clip->audioHash),
                                    r, start, end);
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
