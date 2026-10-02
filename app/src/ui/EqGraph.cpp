#include "EqGraph.h"

#include "ValueText.h"
#include "Theme.h"

#include <collab/ChannelStripDsp.h>

namespace
{
    constexpr double minHz = 20.0, maxHz = 20000.0;
    constexpr double rangeDb = 18.0;              // EQ カーブの表示範囲（±）
    constexpr double maxGainDb = 15.0;            // 設定できるゲイン（±）
    constexpr float spectrumFloorDb = -90.0f;     // スペクトラムの表示範囲（dBFS）

    using ValueText::formatHz;
    juce::String formatDb (double db)     { return ValueText::formatDbUnit (db); }

    /** バンドごとの周波数の範囲。 */
    std::pair<double, double> freqRange (int band)
    {
        switch (band)
        {
            case 0:  return { 20.0, 2000.0 };     // ローカット
            case 1:  return { 20.0, 1000.0 };     // ローシェルフ
            case 2:  return { 40.0, 4000.0 };     // ローミッド
            case 3:  return { 100.0, 16000.0 };   // ミッド
            case 4:  return { 1000.0, 20000.0 };  // ハイシェルフ
            default: return { 1000.0, 20000.0 };  // ハイカット
        }
    }
}

EqGraph::EqGraph (AppContext& c)
    : ctx (c)
{
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    binPower.assign ((size_t) fftSize / 2 + 1, 0.0);
    setRepaintsOnMouseActivity (false);
    startTimerHz (30);
}

EqGraph::~EqGraph() = default;

void EqGraph::setEq (const collab::ChannelEq& e)
{
    if (e == eq)
        return;

    eq = e;
    repaint();
}

//==============================================================================
juce::Rectangle<float> EqGraph::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (34.0f).withTrimmedBottom (16.0f).reduced (2.0f, 4.0f);
}

float EqGraph::xForFreq (double hz) const
{
    const auto r = plotArea();
    const double t = std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
    return r.getX() + (float) t * r.getWidth();
}

double EqGraph::freqForX (float x) const
{
    const auto r = plotArea();
    const double t = juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth()));
    return minHz * std::pow (maxHz / minHz, t);
}

float EqGraph::yForDb (double db) const
{
    const auto r = plotArea();
    return r.getCentreY() - (float) (db / rangeDb) * r.getHeight() * 0.5f;
}

double EqGraph::dbForY (float y) const
{
    const auto r = plotArea();
    return (r.getCentreY() - y) / (r.getHeight() * 0.5f) * rangeDb;
}

juce::String EqGraph::bandName (int band)
{
    switch (band)
    {
        case lowCut:  return "LC";
        case low:     return "L";
        case lowMid:  return "LM";
        case mid:     return "M";
        case high:    return "H";
        default:      return "HC";
    }
}

juce::Colour EqGraph::bandColour (int band)
{
    switch (band)
    {
        case lowCut:  return juce::Colour (0xffef5350);
        case low:     return juce::Colour (0xffffa726);
        case lowMid:  return juce::Colour (0xffffee58);
        case mid:     return juce::Colour (0xff66bb6a);
        case high:    return juce::Colour (0xff42a5f5);
        default:      return juce::Colour (0xffab47bc);
    }
}

juce::Point<float> EqGraph::nodePosition (int band) const
{
    const auto r = plotArea();

    switch (band)
    {
        case lowCut:  return { eq.lowCutHz > 0.0 ? xForFreq (eq.lowCutHz) : r.getX(), yForDb (0.0) };
        case low:     return { xForFreq (eq.lowFreqHz), yForDb (eq.lowGainDb) };
        case lowMid:  return { xForFreq (eq.lowMidFreqHz), yForDb (eq.lowMidGainDb) };
        case mid:     return { xForFreq (eq.midFreqHz), yForDb (eq.midGainDb) };
        case high:    return { xForFreq (eq.highFreqHz), yForDb (eq.highGainDb) };
        default:      return { eq.highCutHz > 0.0 ? xForFreq (eq.highCutHz) : r.getRight(), yForDb (0.0) };
    }
}

int EqGraph::bandAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = 12.0f;

    for (int b = 0; b < numBands; ++b)
    {
        const float d = nodePosition (b).getDistanceFrom (p);

        if (d < bestDistance)
        {
            best = b;
            bestDistance = d;
        }
    }

    return best;
}

juce::String EqGraph::describe (int band) const
{
    switch (band)
    {
        case lowCut:  return "ローカット: "_ju + (eq.lowCutHz > 0.0 ? formatHz (eq.lowCutHz) : "オフ"_ju);
        case low:     return "Low: "_ju + formatDb (eq.lowGainDb) + " / " + formatHz (eq.lowFreqHz);
        case lowMid:  return "Low Mid: "_ju + formatDb (eq.lowMidGainDb) + " / " + formatHz (eq.lowMidFreqHz) + " / Q " + juce::String (eq.lowMidQ, 2);
        case mid:     return "Mid: "_ju + formatDb (eq.midGainDb) + " / " + formatHz (eq.midFreqHz) + " / Q " + juce::String (eq.midQ, 2);
        case high:    return "High: "_ju + formatDb (eq.highGainDb) + " / " + formatHz (eq.highFreqHz);
        default:      return "ハイカット: "_ju + (eq.highCutHz > 0.0 ? formatHz (eq.highCutHz) : "オフ"_ju);
    }
}

//==============================================================================
void EqGraph::timerCallback()
{
    // 再生中だけ新しい音を読む。止まっているときはゆっくり下げて消す
    bool fresh = false;

    if (isShowing() && ctx.engine.isPlaying() && ctx.engine.getSpectrumSamples (fftData.data(), fftSize, spectrumRate))
    {
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
        fresh = true;
    }

    if (! fresh && ! hasSpectrum)
        return;

    // 時間方向の平滑化（パワーで）: 上がるときは速く、下がるときはゆっくり。Neutron などと同じく滑らかに動く
    const double norm = 4.0 / fftSize;   // ハン窓で 0 dBFS の正弦波が 0 dB になるように

    for (size_t i = 0; i < binPower.size(); ++i)
    {
        const double amp = fresh ? fftData[i] * norm : 0.0;
        const double target = amp * amp;
        auto& p = binPower[i];
        p = target > p ? p + (target - p) * 0.55 : p + (target - p) * 0.14;
    }

    updateShownSpectrum();
    repaint();
}

void EqGraph::updateShownSpectrum()
{
    const double binHz = spectrumRate / fftSize;
    const int last = (int) binPower.size() - 1;
    std::vector<float> raw ((size_t) numPoints);
    bool anything = false;

    for (int i = 0; i < numPoints; ++i)
    {
        const double f = minHz * std::pow (maxHz / minHz, (i + 0.5) / numPoints);

        // 1/6 オクターブの帯域のパワーの平均。帯域がビンより狭い低域はビンの間を補間する
        const double lo = f * std::pow (2.0, -1.0 / 12.0) / binHz, hi = f * std::pow (2.0, 1.0 / 12.0) / binHz;
        double power;

        if (hi - lo < 1.0)
        {
            const double pos = juce::jlimit (1.0, (double) last - 1, f / binHz);
            const int b = (int) pos;
            const double t = pos - b;
            power = binPower[(size_t) b] * (1.0 - t) + binPower[(size_t) b + 1] * t;
        }
        else
        {
            const int b0 = juce::jlimit (1, last, (int) std::ceil (lo)), b1 = juce::jlimit (b0, last, (int) std::floor (hi));
            power = 0.0;

            for (int b = b0; b <= b1; ++b)
                power += binPower[(size_t) b];

            power /= (b1 - b0 + 1);
        }

        // 1 kHz を中心に +4.5 dB/oct 傾けて、普通の曲がおおよそ平らに見えるようにする（無音は傾けずに消す）
        const double db = 10.0 * std::log10 (power + 1e-30) + 4.5 * std::log2 (f / 1000.0);
        raw[(size_t) i] = (float) juce::jmax ((double) spectrumFloorDb, db);
        anything = anything || raw[(size_t) i] > spectrumFloorDb + 1.0f;
    }

    // 周波数方向にもならす（2 回）
    for (int pass = 0; pass < 2; ++pass)
    {
        auto copy = raw;

        for (int i = 1; i < numPoints - 1; ++i)
            raw[(size_t) i] = 0.25f * copy[(size_t) i - 1] + 0.5f * copy[(size_t) i] + 0.25f * copy[(size_t) i + 1];
    }

    shownDb = std::move (raw);
    hasSpectrum = anything;
}

//==============================================================================
void EqGraph::paint (juce::Graphics& g)
{
    const auto r = plotArea();
    g.setColour (Theme::field);
    g.fillRect (r);

    // グリッド
    g.setFont (juce::FontOptions (13.0f));

    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xForFreq (hz);
        g.setColour (Theme::gridBeat);
        g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
        g.setColour (Theme::textDim);
        g.drawText (hz >= 1000.0 ? juce::String (juce::roundToInt (hz / 1000.0)) + "k" : juce::String (juce::roundToInt (hz)),
                    juce::Rectangle<float> (x - 20.0f, r.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
    }

    for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
    {
        const float y = yForDb (db);
        g.setColour (std::abs (db) < 0.1 ? Theme::gridBar : Theme::gridBeat);
        g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
        g.setColour (Theme::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 6.0f, r.getX() - 4.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    // スペクトラム（なめらかな曲線で塗る）
    if (hasSpectrum && (int) shownDb.size() == numPoints)
    {
        auto pointAt = [&] (int i)
        {
            const double f = minHz * std::pow (maxHz / minHz, (i + 0.5) / numPoints);
            const float norm = juce::jlimit (0.0f, 1.0f, (shownDb[(size_t) i] - spectrumFloorDb) / -spectrumFloorDb);
            return juce::Point<float> (xForFreq (f), r.getBottom() - norm * r.getHeight());
        };

        juce::Path line;
        auto prev = pointAt (0);
        line.startNewSubPath (r.getX(), prev.y);
        line.lineTo (prev);

        for (int i = 1; i < numPoints; ++i)
        {
            const auto p = pointAt (i);
            line.quadraticTo (prev, (prev + p) * 0.5f);
            prev = p;
        }

        line.lineTo (r.getRight(), prev.y);

        juce::Path fill (line);
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();

        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        g.setColour (Theme::accent.withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (Theme::accent.withAlpha (0.55f));
        g.strokePath (line, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();
    }

    // EQ カーブ（オフのときも形は薄く見せる）
    {
        auto shown = eq;
        shown.enabled = true;
        const double sr = spectrumRate > 0 ? spectrumRate : 48000.0;
        juce::Path curve;
        bool first = true;

        for (float x = r.getX(); x <= r.getRight(); x += 1.0f)
        {
            const double db = juce::jlimit (-rangeDb * 1.5, rangeDb * 1.5, collab::eqResponseDb (shown, sr, freqForX (x)));
            const float y = juce::jlimit (r.getY(), r.getBottom(), yForDb (db));

            if (first)
                curve.startNewSubPath (x, y);
            else
                curve.lineTo (x, y);

            first = false;
        }

        const auto colour = eq.enabled ? Theme::selection : Theme::textDim.withAlpha (0.6f);
        juce::Path fill (curve);
        fill.lineTo (r.getRight(), yForDb (0.0));
        fill.lineTo (r.getX(), yForDb (0.0));
        fill.closeSubPath();
        g.setColour (colour.withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (curve, juce::PathStrokeType (2.0f));
    }

    // ノード
    for (int b = 0; b < numBands; ++b)
    {
        const auto pos = nodePosition (b);
        const bool off = (b == lowCut && eq.lowCutHz <= 0.0) || (b == highCut && eq.highCutHz <= 0.0);
        const bool active = b == hoverBand || b == dragBand;
        const float radius = active ? 8.0f : 6.5f;
        auto colour = bandColour (b);

        if (! eq.enabled || off)
            colour = colour.withMultipliedSaturation (0.3f).withAlpha (0.7f);

        g.setColour (colour);
        g.fillEllipse (pos.x - radius, pos.y - radius, radius * 2, radius * 2);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (bandName (b), juce::Rectangle<float> (pos.x - 10.0f, pos.y - 6.0f, 20.0f, 12.0f), juce::Justification::centred);
    }

    // マウスを乗せたバンドの値
    if (const int b = dragBand >= 0 ? dragBand : hoverBand; b >= 0)
    {
        const auto text = describe (b);
        g.setFont (juce::FontOptions (15.0f));
        auto box = juce::Rectangle<float> (r.getX() + 6.0f, r.getY() + 6.0f, 260.0f, 20.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (bandColour (b));
        g.drawText (text, box.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
    }

    g.setColour (Theme::gridBar);
    g.drawRect (r);
}

//==============================================================================
void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    const int b = bandAt (e.position);

    if (b != hoverBand)
    {
        hoverBand = b;
        setMouseCursor (b >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqGraph::mouseExit (const juce::MouseEvent&)
{
    if (hoverBand >= 0 && dragBand < 0)
    {
        hoverBand = -1;
        repaint();
    }
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    dragBand = bandAt (e.position);
    merging = false;
    repaint();
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0 || onEdit == nullptr)
        return;

    const int band = dragBand;
    const auto [lo, hi] = freqRange (band);
    const auto r = plotArea();
    double hz = juce::jlimit (lo, hi, freqForX (e.position.x));
    hz = hz >= 1000.0 ? std::round (hz / 10.0) * 10.0 : std::round (hz);
    const double db = juce::jlimit (-maxGainDb, maxGainDb, std::round (dbForY (e.position.y) * 10.0) / 10.0);

    // ローカット・ハイカットは端まで持っていくとオフ
    const bool cutOff = (band == lowCut && e.position.x <= r.getX() + 3.0f)
                     || (band == highCut && e.position.x >= r.getRight() - 3.0f);

    onEdit ("EQ "_ju + bandName (band), [band, hz, db, cutOff] (collab::ChannelEq& q)
    {
        q.enabled = true;

        switch (band)
        {
            case lowCut:   q.lowCutHz = cutOff ? 0.0 : hz; break;
            case low:      q.lowFreqHz = hz; q.lowGainDb = db; break;
            case lowMid:   q.lowMidFreqHz = hz; q.lowMidGainDb = db; break;
            case mid:      q.midFreqHz = hz; q.midGainDb = db; break;
            case high:     q.highFreqHz = hz; q.highGainDb = db; break;
            default:       q.highCutHz = cutOff ? 0.0 : hz; break;
        }
    }, merging);

    merging = true;
}

void EqGraph::mouseUp (const juce::MouseEvent& e)
{
    if (merging && onEditEnd != nullptr)
        onEditEnd();

    merging = false;
    dragBand = -1;
    mouseMove (e);
    repaint();
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int band = bandAt (e.position);

    if (band < 0 || onEdit == nullptr)
        return;

    onEdit ("EQ "_ju + bandName (band) + " をリセット"_ju, [band] (collab::ChannelEq& q)
    {
        const collab::ChannelEq d;

        switch (band)
        {
            case lowCut:   q.lowCutHz = d.lowCutHz; break;
            case low:      q.lowFreqHz = d.lowFreqHz; q.lowGainDb = d.lowGainDb; break;
            case lowMid:   q.lowMidFreqHz = d.lowMidFreqHz; q.lowMidGainDb = d.lowMidGainDb; q.lowMidQ = d.lowMidQ; break;
            case mid:      q.midFreqHz = d.midFreqHz; q.midGainDb = d.midGainDb; q.midQ = d.midQ; break;
            case high:     q.highFreqHz = d.highFreqHz; q.highGainDb = d.highGainDb; break;
            default:       q.highCutHz = d.highCutHz; break;
        }
    }, false);

    if (onEditEnd != nullptr)
        onEditEnd();
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int band = bandAt (e.position);

    if ((band != lowMid && band != mid) || onEdit == nullptr || std::abs (w.deltaY) <= 0.0f)
        return;

    const double factor = w.deltaY > 0 ? 1.12 : 1.0 / 1.12;

    onEdit ("EQ "_ju + bandName (band) + " Q"_ju, [band, factor] (collab::ChannelEq& q)
    {
        auto& value = band == lowMid ? q.lowMidQ : q.midQ;
        value = juce::jlimit (0.3, 8.0, std::round (value * factor * 100.0) / 100.0);
    }, false);

    if (onEditEnd != nullptr)
        onEditEnd();
}
