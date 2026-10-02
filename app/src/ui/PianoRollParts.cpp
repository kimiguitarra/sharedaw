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
VelocityLane::VelocityLane (PianoRollView& o) : owner (o)
{
    setTooltip ({});
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

    // 棒の上をぴったり押さなくてよいように、左右 7 ピクセルまで拾う
    x1 -= 7.0f;
    x2 += 7.0f;

    const int vel = juce::jlimit (1, 127, (int) std::round ((1.0f - (y - 4.0f) / ((float) getHeight() - 4.0f)) * 127.0f));
    const auto t1 = owner.axis().xToTick (x1) - (double) clip->startTick;
    const auto t2 = owner.axis().xToTick (x2) - (double) clip->startTick;

    // 選択中のノートがその場所にあるときだけ選択中のノートに絞る（選んでいなくても、棒を触ればそのまま変えられる）
    auto sel = owner.selectedNotes;
    bool touchesSelection = false;

    for (auto& n : clip->notes)
        if ((double) n.tick >= t1 && (double) n.tick <= t2 && sel.count (n.id) > 0)
            touchesSelection = true;

    if (! touchesSelection)
        sel.clear();

    owner.lastVelocity = vel;
    owner.editNotes ("ベロシティの変更"_ju, [t1, t2, vel, sel] (collab::MidiClip& c)
    {
        for (auto& n : c.notes)
            if ((double) n.tick >= t1 && (double) n.tick <= t2 && (sel.empty() || sel.count (n.id) > 0))
                n.velocity = vel;
    }, mergeId);
}

void VelocityLane::mouseMove (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
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
                                  juce::Decibels::decibelsToGain ((float) clip->gainDb), Theme::clipWave (colour));
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
