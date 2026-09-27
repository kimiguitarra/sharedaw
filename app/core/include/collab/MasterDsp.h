#pragma once

// マスターの信号処理: ヴィンテージ系リミッターとラウドネス（LUFS）計測。JUCE に依存しない（テストから使えるように）。

#include <atomic>
#include <vector>

#include "ChannelStripDsp.h"
#include "Project.h"

namespace collab
{

/**
    ヴィンテージ系のリミッター（Ozone の Vintage Limiter の操作感を目標にしたもの）。
    1) THRESHOLD の分だけ入力を持ち上げる
    2) 音楽的にかかる段（ソフトニーのピーク検出。CHARACTER で速さ、モードで性格が変わる）
       - Analog: やわらかいニー、かかり続けるほどリリースが遅くなる（プログラム依存）
       - Tube:   Analog に真空管風の偶数次の倍音を少し足す（かかるほど濃くなる）
       - Modern: ニーが狭く速い、色付けなし
    3) 先読み（約 1.5 ms）のブリックウォールで CEILING を必ず守る
*/
class VintageLimiterDsp
{
public:
    static constexpr int maxChannels = 2;

    void prepare (double sampleRate);
    void reset();
    void setParams (const MasterLimiter&);

    void process (float* const* channels, int numChannels, int numSamples);

    /** 直近のゲインリダクション（dB、0 以上）。 */
    float getGainReductionDb() const noexcept      { return gainReductionDb.load (std::memory_order_relaxed); }

    /** 先読みによる遅れ（サンプル数）。 */
    int getLatencySamples() const noexcept          { return lookahead - 1; }

private:
    double sampleRate = 48000.0;
    MasterLimiter params;

    double inputGain = 1.0, ceilingGain = 1.0, ceilingDb = -1.0, knee = 6.0;
    double attackCoef = 0.0, releaseCoef = 0.0;
    double envDb = 0.0, memory = 0.0;
    double dcState[maxChannels] {}, dcPrev[maxChannels] {};

    int lookahead = 72;
    std::vector<float> delay[maxChannels];
    std::vector<double> required, hold;
    int writePos = 0;
    double holdSum = 0.0;
    std::atomic<float> gainReductionDb { 0.0f };

    void updateCoefficients();
};

/** ITU-R BS.1770 の K 特性フィルタをかけて、100 ms ごとの平均パワー（左右の和）を出す。 */
class LoudnessBlocks
{
public:
    void prepare (double sampleRate);
    void reset();

    /** 100 ms 分が貯まるたびに out へ平均パワーを追加する。 */
    void process (const float* const* channels, int numChannels, int numSamples, std::vector<double>& out);

private:
    Biquad shelf, highPass;
    BiquadState states[2][2];
    double sums[2] {};
    int blockLength = 4800, count = 0;
};

/**
    ラウドネスの集計（EBU R128 / ITU-R BS.1770-4）。
    モーメンタリー（400 ms）、ショートターム（3 s）、インテグレーテッド（ゲート付き、リセットからの全体）。
*/
class LoudnessStats
{
public:
    void reset()                                    { blocks.clear(); }
    void addBlock (double meanSquare)               { blocks.push_back (meanSquare); }

    /** 値がないとき（音が足りない・無音）は -inf の代わりに -100 を返す。 */
    double momentaryLufs() const                    { return windowLufs (4); }
    double shortTermLufs() const                    { return windowLufs (30); }
    double integratedLufs() const;

    /** 計測した長さ（秒）。 */
    double seconds() const noexcept                 { return (double) blocks.size() * 0.1; }

    static double toLufs (double meanSquare);

private:
    std::vector<double> blocks;
    double windowLufs (int numBlocks) const;
};

} // namespace collab
