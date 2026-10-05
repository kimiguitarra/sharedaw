#pragma once

// チャンネルストリップ（EQ・コンプ）の信号処理。JUCE に依存しない（テストから使えるように）。

#include <array>
#include <atomic>
#include <vector>

#include "Project.h"
#include "SlidingMax.h"

namespace collab
{

/** RBJ クックブックの双 2 次フィルタ（Direct Form I、チャンネルごとに状態を持つ）。 */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static Biquad highPass (double sampleRate, double freq, double q);
    static Biquad lowPass (double sampleRate, double freq, double q);
    static Biquad lowShelf (double sampleRate, double freq, double gainDb);
    static Biquad highShelf (double sampleRate, double freq, double gainDb);
    static Biquad peak (double sampleRate, double freq, double q, double gainDb);

    /** 周波数 freq での振幅応答（dB）。テスト用。 */
    double magnitudeDb (double sampleRate, double freq) const;
};

struct BiquadState
{
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    float process (const Biquad& c, float in) noexcept
    {
        const double y = c.b0 * in + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1; x1 = in;
        y2 = y1; y1 = y;
        return (float) y;
    }
};

/**
    EQ とコンプ。setParams() で係数を作り、process() でブロックを処理する。
    setParams() と process() は同じスレッドから呼ぶこと（プラグイン側で受け渡しを行う）。
*/
class ChannelStripDsp
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate);
    void reset();
    void setParams (const ChannelStrip&);

    /** channels[ch][i] をその場で処理する（順番は ChannelStrip::compFirst）。 */
    void process (float* const* channels, int numChannels, int numSamples);

    /** 直近のゲインリダクション（dB、0 以上）。 */
    float getGainReductionDb() const noexcept      { return gainReductionDb.load (std::memory_order_relaxed); }

private:
    double sampleRate = 48000.0;
    ChannelStrip params;

    enum { lowCut1, lowCut2, highCut1, highCut2, lowShelf, lowMidPeak, midPeak, highShelf, numBands };
    std::array<Biquad, numBands> coeffs;
    std::array<bool, numBands> active {};
    std::array<std::array<BiquadState, numBands>, maxChannels> states {};

    // コンプ
    double envDb = 0.0;          // 平滑化したゲインリダクション（dB）
    SlidingMax peakHold;         // FET の検出（直近 10 ms の最大値）
    double rmsSquare = 0.0;      // オプティカルの検出（RMS）
    double optoMemory = 0.0;     // オプティカル: 圧縮が続いた度合い（0〜1）。大きいほどリリースが遅い
    double makeupGain = 1.0;
    Biquad sidechainHp;                                       // 低域のスルー（検出側だけにかける）
    bool sidechainActive = false;
    std::array<BiquadState, maxChannels> sidechainStates {};
    std::atomic<float> gainReductionDb { 0.0f };

    void processEq (float* const* channels, int numChannels, int numSamples);
    void processComp (float* const* channels, int numChannels, int numSamples);
};

/** EQ の周波数特性（dB）。グラフの表示用。無効なら 0。 */
double eqResponseDb (const ChannelEq&, double sampleRate, double freq);

/** EQ の係数（有効なバンドだけ）。 */
std::vector<Biquad> eqBiquads (const ChannelEq&, double sampleRate);

/** コンプの静特性（入力レベル dB → ゲインリダクション dB、ソフトニー）。 */
double compGainReductionDb (double levelDb, double thresholdDb, double ratio, double kneeDb);

} // namespace collab
