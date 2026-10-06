#include "BuiltinEffectPlugin.h"

const char* BuiltinEffectPlugin::xmlTypeName = "shareDawBuiltinEffect";

BuiltinEffectPlugin::BuiltinEffectPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

BuiltinEffectPlugin::~BuiltinEffectPlugin()
{
    notifyListenersOfDeletion();
}

void BuiltinEffectPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    sampleRate = info.sampleRate;

    for (auto& v : waveInput)
        v.assign ((size_t) juce::jmax (8192, info.blockSizeSamples * 2), 0.0f);

    // まだ渡していない処理（初めて挿したとき）もこのレートで用意し直す（作ったときは 48 kHz を仮に使っている）
    for (auto* p : { processor.get(), pendingProcessor.get() })
    {
        if (p != nullptr)
        {
            p->prepare (sampleRate);
            p->setParams (current);
        }
    }
}

void BuiltinEffectPlugin::reset()
{
    needsReset = true;
}

void BuiltinEffectPlugin::setEffect (collab::fx::Type t, const nlohmann::json& params)
{
    if (type == t && params == current)
        return;

    const bool newType = type != t;
    type = t;
    isGate = t == collab::fx::Type::noiseGate;
    current = params;

    // 作り直すときはメッセージスレッドで用意してから渡す（音の処理のスレッドで確保しない）
    std::unique_ptr<collab::fx::Processor> fresh;

    if (newType)
    {
        fresh = collab::fx::createProcessor (t);
        fresh->prepare (sampleRate);
        fresh->setParams (params);
    }

    tail = collab::fx::tailSeconds (t, params);

    std::unique_ptr<collab::fx::Processor> toFree;
    {
        const juce::SpinLock::ScopedLockType sl (lock);

        if (fresh != nullptr)
            pendingProcessor = std::move (fresh);

        pending = params;
        hasPending = true;
        toFree = std::move (retired);   // 音の処理のスレッドが外した古い処理は、ここ（メッセージスレッド）で解放する
    }
}

float BuiltinEffectPlugin::getGainReductionDb() const noexcept
{
    return gainReductionDb.load (std::memory_order_relaxed);
}

void BuiltinEffectPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    if (hasPending.load())
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);

        // 古い処理を置く場所が空いていなければ、入れ替えは次のブロックに回す（ここでは解放もメモリの確保もしない）
        if (sl.isLocked() && (pendingProcessor == nullptr || retired == nullptr))
        {
            if (pendingProcessor != nullptr)
            {
                retired = std::move (processor);
                processor = std::move (pendingProcessor);
            }

            if (processor != nullptr)
                processor->setParams (pending);

            hasPending = false;
        }
    }

    if (processor == nullptr)
        return;

    if (needsReset.exchange (false))
        processor->reset();

    auto& buffer = *fc.destBuffer;
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);

    if (numChannels <= 0)
        return;

    float* channels[2] = {};

    for (int ch = 0; ch < numChannels; ++ch)
        channels[ch] = buffer.getWritePointer (ch, fc.bufferStartSample);

    // 表示用: 入力・出力のピークと、ゲインリダクションの最大（読まれるまで大きい方を残す）
    auto peakOf = [&]
    {
        float p = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            p = juce::jmax (p, juce::FloatVectorOperations::findMaximum (channels[ch], fc.bufferNumSamples),
                            -juce::FloatVectorOperations::findMinimum (channels[ch], fc.bufferNumSamples));

        return p;
    };

    auto keepMax = [] (std::atomic<float>& a, float v)
    {
        auto old = a.load (std::memory_order_relaxed);

        while (v > old && ! a.compare_exchange_weak (old, v, std::memory_order_relaxed)) {}
    };

    keepMax (inputPeak, peakOf());

    // ノイズゲートは、処理する前の音を取っておいて、波形（入力・出力）を表示できるようにする
    const bool wave = isGate.load (std::memory_order_relaxed) && fc.bufferNumSamples <= (int) waveInput[0].size();

    if (wave)
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy (channels[ch], channels[ch] + fc.bufferNumSamples, waveInput[(size_t) ch].begin());

    processor->process (channels, numChannels, fc.bufferNumSamples);
    const float gr = processor->getGainReductionDb();
    gainReductionDb.store (gr, std::memory_order_relaxed);
    keepMax (outputPeak, peakOf());
    keepMax (gainReductionHold, gr);

    if (wave)
        captureWave (channels, numChannels, fc.bufferNumSamples, gr);
}

void BuiltinEffectPlugin::captureWave (float* const* channels, int numChannels, int numSamples, float gr)
{
    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float in = waveInput[(size_t) ch][(size_t) i], out = channels[ch][i];
            waveAcc.inMin = juce::jmin (waveAcc.inMin, in);
            waveAcc.inMax = juce::jmax (waveAcc.inMax, in);
            waveAcc.outMin = juce::jmin (waveAcc.outMin, out);
            waveAcc.outMax = juce::jmax (waveAcc.outMax, out);
        }

        if (++waveCount >= waveColumnSamples)
        {
            waveAcc.gainReductionDb = gr;
            const auto n = waveWritten.load (std::memory_order_relaxed);
            waveRing[(size_t) (n % waveRingSize)] = waveAcc;
            waveWritten.store (n + 1, std::memory_order_release);
            waveAcc = {};
            waveCount = 0;
        }
    }
}

void BuiltinEffectPlugin::readWave (juce::uint64& cursor, std::vector<WaveColumn>& out) const
{
    const auto written = waveWritten.load (std::memory_order_acquire);

    // 遅れすぎたら（画面を閉じていた間など）、残っている分の新しい方から
    if (written > cursor + (juce::uint64) (waveRingSize - 64))
        cursor = written - (juce::uint64) (waveRingSize - 64);

    for (; cursor < written; ++cursor)
        out.push_back (waveRing[(size_t) (cursor % waveRingSize)]);
}

BuiltinEffectPlugin::Meter BuiltinEffectPlugin::takeMeter() noexcept
{
    return { inputPeak.exchange (0.0f), outputPeak.exchange (0.0f), gainReductionHold.exchange (0.0f) };
}
