#pragma once

#include "Common.h"
#include "collab/BuiltinEffects.h"

/**
    内蔵エフェクト（バスコンプ・サチュレーター・リバーブ）をトラックのインサートに挿すプラグイン。
    値はプロジェクト JSON が正なので Edit には保存せず、EngineBridge が setEffect() で渡す。
*/
class BuiltinEffectPlugin  : public te::Plugin
{
public:
    BuiltinEffectPlugin (te::PluginCreationInfo);
    ~BuiltinEffectPlugin() override;

    static const char* getPluginName()                          { return "ShareDAWBuiltinEffect"; }
    static const char* xmlTypeName;

    juce::String getName() const override                       { return getPluginName(); }
    juce::String getPluginType() override                       { return xmlTypeName; }
    juce::String getSelectableDescription() override            { return getName(); }

    bool takesAudioInput() override                             { return true; }
    bool producesAudioWhenNoAudioInput() override               { return true; }   // リバーブの余韻
    int getNumOutputChannelsGivenInputs (int numInputs) override { return juce::jmax (2, numInputs); }
    double getTailLength() const override                       { return tail.load(); }

    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    /** 種類と値を渡す（メッセージスレッドから）。種類が変わったら作り直す。 */
    void setEffect (collab::fx::Type, const nlohmann::json& params);

    /** コンプのゲインリダクション（dB）。 */
    float getGainReductionDb() const noexcept;

    /** 前回読んでから今までの、入力・出力のピーク（0〜1）とゲインリダクションの最大（dB）。読んだら戻す（画面の表示用）。 */
    struct Meter { float input = 0.0f, output = 0.0f, gainReductionDb = 0.0f; };
    Meter takeMeter() noexcept;

private:
    juce::SpinLock lock;
    std::unique_ptr<collab::fx::Processor> processor, pendingProcessor;
    std::unique_ptr<collab::fx::Processor> retired;   // 外した古い処理（メッセージスレッドが setEffect で解放する）
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, gainReductionHold { 0.0f };
    std::optional<collab::fx::Type> type;
    nlohmann::json current, pending;
    std::atomic<bool> hasPending { false }, needsReset { false };
    std::atomic<double> tail { 0.0 };
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltinEffectPlugin)
};
