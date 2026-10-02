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

    processor->process (channels, numChannels, fc.bufferNumSamples);
    gainReductionDb.store (processor->getGainReductionDb(), std::memory_order_relaxed);
}
