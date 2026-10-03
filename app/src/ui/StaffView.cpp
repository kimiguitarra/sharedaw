#include "StaffView.h"

#include "PianoRoll.h"
#include "Theme.h"
#include "collab/ChordPlayback.h"
#include "collab/chord/Degree.h"

namespace
{
    // 五線の位置は「幹音の数」（C4 = 28）。ト音記号の線は E4〜F5、ヘ音記号の線は G2〜A3
    constexpr int trebleLines[] = { 30, 32, 34, 36, 38 };
    constexpr int bassLines[]   = { 18, 20, 22, 24, 26 };
    constexpr int middleC = 28;

    juce::String accidentalText (int accidental)
    {
        return accidental > 0 ? juce::String::fromUTF8 ("\xe2\x99\xaf")      // ♯
             : accidental < 0 ? juce::String::fromUTF8 ("\xe2\x99\xad")      // ♭
                              : juce::String::fromUTF8 ("\xe2\x99\xae");     // ♮
    }
}

void StaffView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::lane);

    auto* clip = owner.getClip();
    const auto& project = owner.ctx.document.getProject();
    const auto& map = owner.ctx.document.getTempoMap();
    const auto& axis = owner.axis();

    // 線の間隔: 高さに合わせる（大譜表と上下の加線の分が収まるように）
    const float space = juce::jlimit (7.0f, 14.0f, (float) getHeight() / 30.0f);
    const float half = space * 0.5f;
    const float centreY = (float) getHeight() * 0.5f;
    auto yOf = [&] (int step) { return centreY - (float) (step - middleC) * half; };
    auto xOf = [&] (double tick) { return (float) leftWidth + (float) axis.tickToX (tick); };

    const auto lineColour = Theme::textDim.withAlpha (0.8f);

    // 五線
    g.setColour (lineColour);

    for (int step : trebleLines)
        g.fillRect (0.0f, yOf (step) - 0.5f, (float) getWidth(), 1.0f);

    for (int step : bassLines)
        g.fillRect (0.0f, yOf (step) - 0.5f, (float) getWidth(), 1.0f);

    // 小節線（見えている範囲）
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (leftWidth, 0, getWidth() - leftWidth, getHeight());
        const auto firstTick = (collab::Tick) std::max (0.0, axis.xToTick (0.0));
        const auto lastTick = (collab::Tick) std::max (0.0, axis.xToTick ((double) (getWidth() - leftWidth)));

        for (int bar = map.tickToBar (firstTick); bar <= map.tickToBar (lastTick) + 1; ++bar)
        {
            const float x = xOf ((double) map.barToTick (bar));
            g.setColour (lineColour);
            g.fillRect (x - 0.5f, yOf (38), 1.0f, yOf (18) - yOf (38));
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (bar), juce::Rectangle<float> (x + 3.0f, yOf (38) - 2.2f * space, 40.0f, 14.0f), juce::Justification::centredLeft, false);
        }
    }

    // 左: 音部記号と調号（表示している範囲の頭のキー）
    const auto leftTick = (collab::Tick) std::max (0.0, axis.xToTick (0.0));
    const auto key = collab::keyAt (project, map, leftTick).value_or (collab::chord::Key {});

    g.setColour (Theme::panel);
    g.fillRect (0, 0, leftWidth, getHeight());
    g.setColour (lineColour);

    for (int step : trebleLines)
        g.fillRect (0.0f, yOf (step) - 0.5f, (float) leftWidth, 1.0f);

    for (int step : bassLines)
        g.fillRect (0.0f, yOf (step) - 0.5f, (float) leftWidth, 1.0f);

    // 音部記号（文字の基準線で位置を合わせる。ト音記号の渦は G4 の線、ヘ音記号の点は F3 の線）
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (space * 6.0f));
    g.drawSingleLineText (juce::String::fromUTF8 ("\xf0\x9d\x84\x9e"), (int) (space * 0.3f), (int) yOf (30) + (int) (space * 0.9f));
    g.setFont (juce::FontOptions (space * 3.6f));
    g.drawSingleLineText (juce::String::fromUTF8 ("\xf0\x9d\x84\xa2"), (int) (space * 0.6f), (int) yOf (18) - (int) (space * 0.1f));

    {
        static const int sharpSteps[] = { 38, 35, 39, 36, 33, 37, 34 };   // F5 C5 G5 D5 A4 E5 B4
        static const int flatSteps[]  = { 34, 37, 33, 36, 32, 35, 31 };   // B4 E5 A4 D5 G4 C5 F4
        const int count = collab::chord::keySignature (key);
        g.setFont (juce::FontOptions (space * 2.2f, juce::Font::bold));

        for (int i = 0; i < std::abs (count); ++i)
        {
            const int step = (count > 0 ? sharpSteps : flatSteps)[i];
            const float x = space * 3.6f + (float) i * space * 0.9f;

            for (int staffOffset : { 0, -14 })   // ヘ音記号の段は 2 オクターブ下
                g.drawText (accidentalText (count > 0 ? 1 : -1),
                            juce::Rectangle<float> (x, yOf (step + staffOffset) - space * 1.2f, space * 1.2f, space * 2.0f),
                            juce::Justification::centred, false);
        }
    }

    g.setColour (Theme::background);
    g.drawVerticalLine (leftWidth - 1, 0.0f, (float) getHeight());

    if (clip == nullptr)
        return;

    // 音符
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (leftWidth, 0, getWidth() - leftWidth, getHeight());

    std::vector<const collab::Note*> notes;

    for (auto& n : clip->notes)
        if (n.tick >= 0 && n.tick < clip->lengthTick)
            notes.push_back (&n);

    std::sort (notes.begin(), notes.end(), [] (auto* a, auto* b) { return a->tick != b->tick ? a->tick < b->tick : a->pitch < b->pitch; });

    // 小節の中で付けた臨時記号（幹音の位置ごと）。小節が変わったら調号に戻る
    std::map<int, int> barAccidentals;
    int currentBar = -1;
    const auto base = Theme::parseColour (owner.getTrack() != nullptr ? owner.getTrack()->color : std::string());

    for (auto* n : notes)
    {
        const auto abs = clip->startTick + n->tick;
        const int bar = map.tickToBar (abs);

        if (bar != currentBar)
        {
            barAccidentals.clear();
            currentBar = bar;
        }

        const auto noteKey = collab::keyAt (project, map, abs).value_or (collab::chord::Key {});
        const auto spelled = collab::chord::spellForStaff (n->pitch, noteKey);
        const int step = spelled.step();
        const float x = xOf ((double) abs);
        const float xEnd = xOf ((double) (abs + n->lengthTick));
        const float y = yOf (step);

        if (xEnd < (float) leftWidth || x > (float) getWidth())
            continue;

        const bool selected = owner.selectedNotes.count (n->id) > 0;
        const auto colour = selected ? Theme::selection : Theme::text;

        // 長さの帯（時間どおりの長さが分かるように）
        g.setColour (base.withAlpha (0.35f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x, y - half * 0.6f, juce::jmax (2.0f, xEnd - x), half * 1.2f), 2.0f);

        // 加線（五線の外。中央の C を含む）
        g.setColour (lineColour);

        auto ledger = [&] (int s) { g.fillRect (x - space * 0.4f, yOf (s) - 0.5f, space * 1.9f, 1.0f); };

        if (step == middleC)
            ledger (middleC);

        for (int s = 40; s <= step; s += 2)
            ledger (s);

        for (int s = 16; s >= step; s -= 2)
            ledger (s);

        // 臨時記号（調号・小節の中で前に付けたものと違うときだけ）
        const int expected = barAccidentals.count (step) > 0 ? barAccidentals[step]
                                                            : collab::chord::keySignatureAccidental (spelled.letter, noteKey);

        g.setColour (colour);

        if (spelled.accidental != expected)
        {
            g.setFont (juce::FontOptions (space * 2.0f, juce::Font::bold));
            g.drawText (accidentalText (spelled.accidental), juce::Rectangle<float> (x - space * 1.5f, y - space * 1.1f, space * 1.3f, space * 2.0f),
                        juce::Justification::centred, false);
            barAccidentals[step] = spelled.accidental;
        }

        // 符頭: 2 分音符以上は白、4 分音符以下は黒。全音符は符尾なし
        const double beats = (double) n->lengthTick / collab::kPpq;
        const auto head = juce::Rectangle<float> (x, y - half * 0.85f, space * 1.25f, half * 1.7f);

        if (beats >= 2.0 - 1.0e-6)
            g.drawEllipse (head.reduced (0.6f), 1.6f);
        else
            g.fillEllipse (head);

        if (beats < 4.0 - 1.0e-6)
        {
            // 符尾: 段の真ん中の線より下の音は上向き、上の音は下向き
            const bool treble = step >= middleC;
            const bool up = step < (treble ? 34 : 22);
            const float stemX = up ? head.getRight() - 0.8f : head.getX() + 0.2f;
            const float stemEnd = up ? y - space * 3.4f : y + space * 3.4f;
            g.fillRect (juce::Rectangle<float> (stemX, std::min (y, stemEnd), 1.3f, std::abs (stemEnd - y)));

            // 旗: 8 分音符 1 本、16 分音符 2 本、それより短いと 3 本
            const int flagCount = beats <= 0.125 + 1.0e-6 ? 3 : beats <= 0.25 + 1.0e-6 ? 2 : beats <= 0.5 + 1.0e-6 ? 1 : 0;

            for (int f = 0; f < flagCount; ++f)
            {
                const float fy = stemEnd + (up ? 1.0f : -1.0f) * (float) f * space * 0.8f;
                juce::Path flag;
                flag.startNewSubPath (stemX + 0.6f, fy);
                flag.quadraticTo (stemX + space * 1.2f, fy + (up ? 1.0f : -1.0f) * space * 0.9f, stemX + space * 0.9f, fy + (up ? 1.0f : -1.0f) * space * 1.8f);
                g.strokePath (flag, juce::PathStrokeType (1.4f));
            }
        }
    }
}

void StaffView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    owner.handleWheel (e, w, this);
}
