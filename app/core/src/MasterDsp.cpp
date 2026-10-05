#include "collab/MasterDsp.h"

#include <algorithm>
#include <cmath>

namespace collab
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    double dbToGain (double db)      { return std::pow (10.0, db / 20.0); }

    double coefficientFor (double seconds, double sampleRate)
    {
        return seconds <= 0.0 ? 0.0 : std::exp (-1.0 / (seconds * sampleRate));
    }

    /** 真空管風の飽和（正負で少し違う曲がり方にして偶数次の倍音を出す）。drive は 0〜1。 */
    double tube (double x, double drive)
    {
        if (drive <= 0.0)
            return x;

        const double k = 1.0 + 2.0 * drive;
        const double y = x >= 0.0 ? std::tanh (k * x) / k : std::tanh (k * 0.8 * x) / (k * 0.8);
        return x + (y - x) * drive;
    }
}

//==============================================================================
void VintageLimiterDsp::prepare (double sr)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    lookahead = std::max (8, (int) std::lround (0.005 * sampleRate));

    for (auto& d : delay)
        d.assign ((size_t) lookahead + 1, 0.0f);

    wallHistory.assign ((size_t) lookahead, 1.0);
    levelHold.prepare ((int) std::lround (0.025 * sampleRate));   // 40 Hz の 1 周期
    wallMin.prepare (lookahead);
    updateCoefficients();
    reset();
}

void VintageLimiterDsp::reset()
{
    for (auto& d : delay)
        std::fill (d.begin(), d.end(), 0.0f);

    std::fill (wallHistory.begin(), wallHistory.end(), 1.0);
    wallSum = (double) wallHistory.size();
    levelHold.reset();
    wallMin.reset();
    writePos = 0;
    envDb = memory = 0.0;
    wallGain = 1.0;

    for (int ch = 0; ch < maxChannels; ++ch)
        dcState[ch] = dcPrev[ch] = 0.0;

    gainReductionDb.store (0.0f);
}

void VintageLimiterDsp::setParams (const MasterLimiter& p)
{
    params = p;
    updateCoefficients();
}

void VintageLimiterDsp::updateCoefficients()
{
    const double c = std::clamp (params.character, 0.0, 10.0) / 10.0;   // 0 = ゆっくり、1 = 速い
    const bool modern = params.mode == LimiterMode::modern;

    inputGain = dbToGain (-std::min (0.0, params.thresholdDb));
    ceilingDb = std::clamp (params.ceilingDb, -12.0, 0.0);
    ceilingGain = dbToGain (ceilingDb);
    knee = modern ? 2.0 : 6.0;

    // アタック 10 ms → 1 ms、リリース 900 ms → 60 ms（Modern は速め）
    const double attackMs = 10.0 * std::pow (0.1, c) * (modern ? 0.6 : 1.0);
    const double releaseMs = 900.0 * std::pow (60.0 / 900.0, c) * (modern ? 0.6 : 1.0);
    attackCoef = coefficientFor (attackMs * 0.001, sampleRate);
    releaseCoef = coefficientFor (releaseMs * 0.001, sampleRate);

    // ブリックウォールの戻り（速すぎると低音の山ごとに量が動いて歪む）
    wallReleaseCoef = coefficientFor ((modern ? 0.05 : 0.08), sampleRate);
}

void VintageLimiterDsp::process (float* const* channels, int numChannels, int numSamples)
{
    numChannels = std::min (numChannels, maxChannels);

    if (numChannels <= 0 || wallHistory.empty())
        return;

    const bool tubeMode = params.mode == LimiterMode::tube;
    const bool analogLike = params.mode != LimiterMode::modern;
    const double memoryUp = 1.0 / (2.0 * sampleRate), memoryDown = 1.0 / (6.0 * sampleRate);
    const double dcCoef = 1.0 - 2.0 * pi * 5.0 / sampleRate;   // 真空管の片寄りで出る直流を取る
    const int L = lookahead;
    const int size = (int) delay[0].size();   // L + 1
    double maxGr = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        double x[maxChannels] {};
        double peak = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            double v = channels[ch][i] * inputGain;

            if (tubeMode)
            {
                // 真空管風の色付け（かかるほど少し濃く。前より控えめ）
                v = tube (v, std::min (1.0, 0.08 + envDb / 30.0) * 0.4);

                const double hp = v - dcPrev[ch] + dcCoef * dcState[ch];
                dcPrev[ch] = v;
                dcState[ch] = hp;
                v = hp;
            }

            x[ch] = v;
            peak = std::max (peak, std::abs (v));
        }

        // 1) 音楽的にかかる段: 大きさは 25 ms の間の最大値（1 周期の中で量を動かさない）
        const double held = levelHold.push (peak);
        const double levelDb = 20.0 * std::log10 (held + 1e-12);
        const double target = compGainReductionDb (levelDb, ceilingDb, 30.0, knee);

        if (target > envDb)
        {
            envDb = target + (envDb - target) * attackCoef;
        }
        else
        {
            // Analog / Tube: かかり続けるほど戻りが遅くなる
            const double rel = analogLike ? std::pow (releaseCoef, 1.0 / (1.0 + 2.0 * memory)) : releaseCoef;
            envDb = target + (envDb - target) * rel;
        }

        if (analogLike)
            memory += envDb > 1.0 ? (1.0 - memory) * memoryUp : -memory * memoryDown;

        const double g1 = dbToGain (-envDb);

        // 2) 先読みのブリックウォール: 先読みの間の最小の必要量を、先読みの長さで平均してゆっくり下げ（その音が出る時には必ず届く）、
        //    戻りはなめらかに（80 ms）
        double peak2 = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            x[ch] *= g1;
            peak2 = std::max (peak2, std::abs (x[ch]));
        }

        const double required = peak2 > ceilingGain ? ceilingGain / peak2 : 1.0;
        const double minimum = -wallMin.push (-required);
        const size_t slot = (size_t) (writePos % L);
        wallSum += minimum - wallHistory[slot];
        wallHistory[slot] = minimum;
        const double attacked = std::min (1.0, wallSum / L);

        wallGain = attacked < wallGain ? attacked : attacked + (wallGain - attacked) * wallReleaseCoef;

        // L サンプル遅らせた音にかける
        const int readPos = (writePos + 1) % size;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& d = delay[ch];
            d[(size_t) writePos] = (float) x[ch];
            const double out = d[(size_t) readPos] * wallGain;
            channels[ch][i] = (float) std::clamp (out, -ceilingGain, ceilingGain);
        }

        writePos = (writePos + 1) % size;
        maxGr = std::max (maxGr, envDb - 20.0 * std::log10 (std::max (1e-6, wallGain)));
    }

    gainReductionDb.store ((float) maxGr, std::memory_order_relaxed);
}

void VintageLimiterDsp::processBypassed (float* const* channels, int numChannels, int numSamples)
{
    numChannels = std::min (numChannels, maxChannels);

    if (numChannels <= 0 || wallHistory.empty())
        return;

    const int L = lookahead;
    const int size = (int) delay[0].size();

    for (int i = 0; i < numSamples; ++i)
    {
        // 先読みの窓は「かけない」で埋めておく（オンに戻したときに古い値が残らないように）
        const size_t slot = (size_t) (writePos % L);
        wallSum += 1.0 - wallHistory[slot];
        wallHistory[slot] = 1.0;
        wallMin.push (-1.0);
        levelHold.push (0.0);
        wallGain = 1.0;

        const int readPos = (writePos + 1) % size;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& d = delay[ch];
            d[(size_t) writePos] = channels[ch][i];
            channels[ch][i] = d[(size_t) readPos];
        }

        writePos = (writePos + 1) % size;
    }

    envDb = 0.0;
    gainReductionDb.store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
void LoudnessBlocks::prepare (double sr)
{
    if (sr <= 0.0)
        sr = 48000.0;

    // BS.1770 の K 特性（任意のサンプルレート用の式。48 kHz で規格の係数と一致する）
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                  2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }

    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        highPass = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }

    blockLength = std::max (1, (int) std::lround (sr * 0.1));
    reset();
}

void LoudnessBlocks::reset()
{
    for (auto& ch : states)
        for (auto& s : ch)
            s = {};

    sums[0] = sums[1] = 0.0;
    count = 0;
}

void LoudnessBlocks::process (const float* const* channels, int numChannels, int numSamples, std::vector<double>& out)
{
    numChannels = std::min (numChannels, 2);

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float y = states[ch][1].process (highPass, states[ch][0].process (shelf, channels[ch][i]));
            sums[ch] += (double) y * y;
        }

        if (++count >= blockLength)
        {
            // モノラルは左右に同じ音があるものとして 2 倍する（ステレオで再生したときと同じ値になるように）
            const double power = numChannels == 1 ? 2.0 * sums[0] : sums[0] + sums[1];
            out.push_back (power / blockLength);
            sums[0] = sums[1] = 0.0;
            count = 0;
        }
    }
}

//==============================================================================
double LoudnessStats::toLufs (double meanSquare)
{
    return meanSquare <= 1e-10 ? -100.0 : -0.691 + 10.0 * std::log10 (meanSquare);
}

double LoudnessStats::windowLufs (int numBlocks) const
{
    if ((int) blocks.size() < numBlocks)
        return -100.0;

    double sum = 0.0;

    for (size_t i = blocks.size() - (size_t) numBlocks; i < blocks.size(); ++i)
        sum += blocks[i];

    return toLufs (sum / numBlocks);
}

double LoudnessStats::integratedLufs() const
{
    // 400 ms の窓（100 ms ずつずらす）ごとのパワー
    if (blocks.size() < 4)
        return -100.0;

    std::vector<double> windows;
    windows.reserve (blocks.size());

    for (size_t i = 3; i < blocks.size(); ++i)
        windows.push_back ((blocks[i - 3] + blocks[i - 2] + blocks[i - 1] + blocks[i]) / 4.0);

    // 絶対ゲート（-70 LUFS）
    double sum = 0.0;
    int n = 0;

    for (double w : windows)
        if (toLufs (w) > -70.0)
        {
            sum += w;
            ++n;
        }

    if (n == 0)
        return -100.0;

    // 相対ゲート（-10 LU）
    const double relative = toLufs (sum / n) - 10.0;
    sum = 0.0;
    n = 0;

    for (double w : windows)
        if (const double l = toLufs (w); l > -70.0 && l > relative)
        {
            sum += w;
            ++n;
        }

    return n == 0 ? -100.0 : toLufs (sum / n);
}

} // namespace collab
