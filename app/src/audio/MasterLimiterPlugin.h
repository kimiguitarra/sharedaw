#pragma once

#include "Common.h"
#include "collab/MasterDsp.h"

/**
    マスターのリミッターとラウドネス計測。曲の音がまとまる内部のミックスバスに挿す（メトロノームは通らない）。
    設定はプロジェクト JSON（Project::master）が正なので Edit には保存せず、EngineBridge が setLimiter() で渡す。
    ラウドネスはリミッターの後の音で測る（マスター音量＝この PC の聞く音量には左右されない）。
*/
class MasterLimiterPlugin  : public te::Plugin
{
public:
    MasterLimiterPlugin (te::PluginCreationInfo);
    ~MasterLimiterPlugin() override;

    static const char* getPluginName()                          { return "ShareDAWMasterLimiter"; }
    static const char* xmlTypeName;

    juce::String getName() const override                       { return getPluginName(); }
    juce::String getPluginType() override                       { return xmlTypeName; }
    juce::String getSelectableDescription() override            { return getName(); }

    bool takesAudioInput() override                             { return true; }
    bool producesAudioWhenNoAudioInput() override               { return false; }
    int getNumOutputChannelsGivenInputs (int numInputs) override { return juce::jmax (2, numInputs); }
    double getLatencySeconds() override;

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    /** 設定を渡す（メッセージスレッドから）。次のブロックから反映される。 */
    void setLimiter (const collab::MasterLimiter&);

    float getGainReductionDb() const noexcept                   { return dsp.getGainReductionDb(); }

    /** 前回読んでからの入力・出力のピーク（dB、左右の大きいほう）。 */
    float takeInputPeakDb()                                     { return juce::Decibels::gainToDecibels (inputPeak.exchange (0.0f), -100.0f); }
    float takeOutputPeakDb()                                    { return juce::Decibels::gainToDecibels (outputPeak.exchange (0.0f), -100.0f); }

    /** 100 ms ごとのラウドネスのパワーを受け取る（メッセージスレッドから）。 */
    void takeLoudnessBlocks (std::vector<double>& out);

    /** イメージャーの表示用に、リミッターの後の直近 numSamples サンプル（左・右）をコピーする（メッセージスレッドから）。 */
    bool getLatestStereo (float* left, float* right, int numSamples) const;
    static constexpr int scopeSize = 8192;

private:
    collab::VintageLimiterDsp dsp;
    collab::LoudnessBlocks loudness;
    collab::MasterLimiter current, pending;
    juce::SpinLock lock;
    std::atomic<bool> hasPending { false }, needsReset { false }, limiterEnabled { false };
    double sampleRate = 48000.0;

    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::vector<double> blockScratch;

    // イメージャー用の左右の音（表示だけなので、読む途中で書き換わっても構わない）
    std::array<float, scopeSize> scopeLeft {}, scopeRight {};
    std::atomic<int> scopeWrite { 0 };

    // ラウドネスのブロック（1 時間分まで貯められる）
    static constexpr int fifoSize = 36000;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<double> fifoData = std::vector<double> ((size_t) fifoSize);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterLimiterPlugin)
};
