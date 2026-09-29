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

    if (processor != nullptr)
    {
        processor->prepare (sampleRate);
        processor->setParams (current);
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
        tail = fresh->tailSeconds();
    }

    const juce::SpinLock::ScopedLockType sl (lock);

    if (fresh != nullptr)
        pendingProcessor = std::move (fresh);

    pending = params;
    hasPending = true;
}

float BuiltinEffectPlugin::getGainReductionDb() const noexcept
{
    auto* p = processor.get();
    return p != nullptr ? p->getGainReductionDb() : 0.0f;
}

void BuiltinEffectPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    std::unique_ptr<collab::fx::Processor> old;

    if (hasPending.load())
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);

        if (sl.isLocked())
        {
            if (pendingProcessor != nullptr)
            {
                old = std::move (processor);
                processor = std::move (pendingProcessor);
            }

            if (processor != nullptr)
            {
                processor->setParams (pending);
                tail = processor->tailSeconds();
            }

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

    // 古い処理はメッセージスレッドで捨てる（ここで解放しない）
    if (old != nullptr)
        juce::MessageManager::callAsync ([p = std::shared_ptr<collab::fx::Processor> (std::move (old))] {});
}
