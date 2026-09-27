#include "collab/ChannelStripDsp.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace collab
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    double dbToGain (double db)       { return std::pow (10.0, db / 20.0); }

    double clampFreq (double sampleRate, double f)
    {
        return std::clamp (f, 10.0, sampleRate * 0.45);
    }

    Biquad normalise (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    /** 1 次のローパスの係数（時定数 seconds）。 */
    double onePole (double seconds, double sampleRate)
    {
        return seconds <= 0.0 ? 0.0 : std::exp (-1.0 / (seconds * sampleRate));
    }
}

//==============================================================================
Biquad Biquad::highPass (double sr, double freq, double q)
{
    const double w = 2.0 * pi * clampFreq (sr, freq) / sr;
    const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    return normalise ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
}

Biquad Biquad::lowPass (double sr, double freq, double q)
{
    const double w = 2.0 * pi * clampFreq (sr, freq) / sr;
    const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
    return normalise ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
}

Biquad Biquad::lowShelf (double sr, double freq, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * pi * clampFreq (sr, freq) / sr;
    const double cw = std::cos (w), alpha = std::sin (w) / 2.0 * std::sqrt (2.0);   // S = 1
    const double s = 2.0 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1) - (A - 1) * cw + s), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - s),
                      (A + 1) + (A - 1) * cw + s, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - s);
}

Biquad Biquad::highShelf (double sr, double freq, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * pi * clampFreq (sr, freq) / sr;
    const double cw = std::cos (w), alpha = std::sin (w) / 2.0 * std::sqrt (2.0);
    const double s = 2.0 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1) + (A - 1) * cw + s), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - s),
                      (A + 1) - (A - 1) * cw + s, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - s);
}

Biquad Biquad::peak (double sr, double freq, double q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * pi * clampFreq (sr, freq) / sr;
    const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * std::max (0.05, q));
    return normalise (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
}

double Biquad::magnitudeDb (double sr, double freq) const
{
    const auto z = std::polar (1.0, -2.0 * pi * freq / sr);
    const auto h = (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
    return 20.0 * std::log10 (std::max (1e-12, std::abs (h)));
}

//==============================================================================
std::vector<Biquad> eqBiquads (const ChannelEq& eq, double sr)
{
    std::vector<Biquad> r;

    if (! eq.enabled)
        return r;

    if (eq.lowCutHz > 0.0)
    {
        r.push_back (Biquad::highPass (sr, eq.lowCutHz, 0.5412));
        r.push_back (Biquad::highPass (sr, eq.lowCutHz, 1.3066));
    }

    if (eq.highCutHz > 0.0)
    {
        r.push_back (Biquad::lowPass (sr, eq.highCutHz, 0.5412));
        r.push_back (Biquad::lowPass (sr, eq.highCutHz, 1.3066));
    }

    if (std::abs (eq.lowGainDb) > 0.01)    r.push_back (Biquad::lowShelf (sr, eq.lowFreqHz, eq.lowGainDb));
    if (std::abs (eq.lowMidGainDb) > 0.01) r.push_back (Biquad::peak (sr, eq.lowMidFreqHz, eq.lowMidQ, eq.lowMidGainDb));
    if (std::abs (eq.midGainDb) > 0.01)    r.push_back (Biquad::peak (sr, eq.midFreqHz, eq.midQ, eq.midGainDb));
    if (std::abs (eq.highGainDb) > 0.01)   r.push_back (Biquad::highShelf (sr, eq.highFreqHz, eq.highGainDb));
    return r;
}

double eqResponseDb (const ChannelEq& eq, double sr, double freq)
{
    double db = 0.0;

    for (auto& b : eqBiquads (eq, sr))
        db += b.magnitudeDb (sr, freq);

    return db;
}

//==============================================================================
double compGainReductionDb (double levelDb, double thresholdDb, double ratio, double kneeDb)
{
    const double slope = 1.0 - 1.0 / std::max (1.0, ratio);
    const double over = levelDb - thresholdDb;

    if (kneeDb > 0.0 && std::abs (over) <= kneeDb / 2.0)
    {
        const double x = over + kneeDb / 2.0;
        return slope * x * x / (2.0 * kneeDb);
    }

    return over > 0.0 ? slope * over : 0.0;
}

void ChannelStripDsp::prepare (double sr)
{
    sampleRate = sr > 0 ? sr : 48000.0;
    setParams (params);
    reset();
}

void ChannelStripDsp::reset()
{
    for (auto& ch : states)
        for (auto& s : ch)
            s = {};

    envDb = 0.0;
    rmsSquare = 0.0;
    optoMemory = 0.0;
    gainReductionDb.store (0.0f);
}

void ChannelStripDsp::setParams (const ChannelStrip& p)
{
    params = p;
    const auto& eq = p.eq;

    // ローカットは 4 次のバターワース（24dB/oct）
    active[lowCut1] = active[lowCut2] = eq.lowCutHz > 0.0;
    coeffs[lowCut1] = Biquad::highPass (sampleRate, eq.lowCutHz, 0.5412);
    coeffs[lowCut2] = Biquad::highPass (sampleRate, eq.lowCutHz, 1.3066);

    active[highCut1] = active[highCut2] = eq.highCutHz > 0.0;
    coeffs[highCut1] = Biquad::lowPass (sampleRate, eq.highCutHz, 0.5412);
    coeffs[highCut2] = Biquad::lowPass (sampleRate, eq.highCutHz, 1.3066);

    active[lowMidPeak] = std::abs (eq.lowMidGainDb) > 0.01;
    coeffs[lowMidPeak] = Biquad::peak (sampleRate, eq.lowMidFreqHz, eq.lowMidQ, eq.lowMidGainDb);

    active[lowShelf] = std::abs (eq.lowGainDb) > 0.01;
    coeffs[lowShelf] = Biquad::lowShelf (sampleRate, eq.lowFreqHz, eq.lowGainDb);

    active[midPeak] = std::abs (eq.midGainDb) > 0.01;
    coeffs[midPeak] = Biquad::peak (sampleRate, eq.midFreqHz, eq.midQ, eq.midGainDb);

    active[highShelf] = std::abs (eq.highGainDb) > 0.01;
    coeffs[highShelf] = Biquad::highShelf (sampleRate, eq.highFreqHz, eq.highGainDb);

    makeupGain = dbToGain (p.comp.makeupDb);

    if (! p.comp.enabled)
    {
        envDb = 0.0;
        gainReductionDb.store (0.0f);
    }
}

void ChannelStripDsp::process (float* const* channels, int numChannels, int numSamples)
{
    numChannels = std::min (numChannels, maxChannels);

    if (params.eq.enabled)
        processEq (channels, numChannels, numSamples);

    if (params.comp.enabled)
        processComp (channels, numChannels, numSamples);
}

void ChannelStripDsp::processEq (float* const* channels, int numChannels, int numSamples)
{
    for (int band = 0; band < numBands; ++band)
    {
        if (! active[(size_t) band])
            continue;

        const auto& c = coeffs[(size_t) band];

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& st = states[(size_t) ch][(size_t) band];
            auto* d = channels[ch];

            for (int i = 0; i < numSamples; ++i)
                d[i] = st.process (c, d[i]);
        }
    }
}

void ChannelStripDsp::processComp (float* const* channels, int numChannels, int numSamples)
{
    const auto& c = params.comp;
    const bool opto = c.type == CompType::opto;

    // FET: ピーク検出・速いアタック・ハードに近いニー。オプティカル: RMS 検出・ゆっくり・広いニー、
    // リリースは圧縮が続くほど遅くなる（LA-2A のように 2 段階で戻る感じ）
    const double knee = opto ? 10.0 : 2.0;
    const double attack = onePole (opto ? 0.010 : std::max (0.00002, c.attackMs * 0.001), sampleRate);
    const double fetRelease = onePole (std::max (0.001, c.releaseMs * 0.001), sampleRate);
    const double rmsCoef = onePole (0.005, sampleRate);
    const double memoryUp = 1.0 / (1.5 * sampleRate), memoryDown = 1.0 / (4.0 * sampleRate);
    double maxGr = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        double peak = 0.0, square = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const double x = channels[ch][i];
            peak = std::max (peak, std::abs (x));
            square = std::max (square, x * x);
        }

        double levelDb;

        if (opto)
        {
            rmsSquare = rmsCoef * rmsSquare + (1.0 - rmsCoef) * square;
            levelDb = 10.0 * std::log10 (rmsSquare + 1e-20);
        }
        else
        {
            levelDb = 20.0 * std::log10 (peak + 1e-10);
        }

        const double target = compGainReductionDb (levelDb, c.thresholdDb, c.ratio, knee);

        if (target > envDb)
        {
            envDb = target + (envDb - target) * attack;
        }
        else
        {
            double release = fetRelease;

            if (opto)
            {
                const double seconds = 0.06 + 1.5 * optoMemory;
                release = 1.0 - 1.0 / (seconds * sampleRate);
            }

            envDb = target + (envDb - target) * release;
        }

        if (opto)
            optoMemory += envDb > 1.0 ? (1.0 - optoMemory) * memoryUp : -optoMemory * memoryDown;

        maxGr = std::max (maxGr, envDb);
        const double gain = std::pow (10.0, -envDb / 20.0) * makeupGain;

        // FET は圧縮が深いほど少し歪む（色付け）
        const double drive = opto ? 0.0 : std::min (envDb / 24.0, 1.0) * 0.2;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            double y = channels[ch][i] * gain;

            if (drive > 0.0)
                y = (1.0 - drive) * y + drive * std::tanh (y);

            channels[ch][i] = (float) y;
        }
    }

    gainReductionDb.store ((float) maxGr, std::memory_order_relaxed);
}

} // namespace collab
