#include "ChannelStripPlugin.h"

const char* ChannelStripPlugin::xmlTypeName = "shareDawChannelStrip";

ChannelStripPlugin::ChannelStripPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
}

ChannelStripPlugin::~ChannelStripPlugin()
{
    notifyListenersOfDeletion();
}

void ChannelStripPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    dsp.setParams (current);
    dsp.prepare (info.sampleRate);
}

void ChannelStripPlugin::reset()
{
    needsReset = true;
}

void ChannelStripPlugin::setStrip (const collab::ChannelStrip& s)
{
    if (s == current)
        return;

    current = s;
    const juce::SpinLock::ScopedLockType sl (lock);
    pending = s;
    hasPending = true;
}

void ChannelStripPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    if (hasPending.load())
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);

        if (sl.isLocked())
        {
            dsp.setParams (pending);
            hasPending = false;
        }
    }

    if (needsReset.exchange (false))
        dsp.reset();

    auto& buffer = *fc.destBuffer;
    const int numChannels = juce::jmin (buffer.getNumChannels(), collab::ChannelStripDsp::maxChannels);

    if (numChannels <= 0)
        return;

    float* channels[collab::ChannelStripDsp::maxChannels] = {};

    for (int ch = 0; ch < numChannels; ++ch)
        channels[ch] = buffer.getWritePointer (ch, fc.bufferStartSample);

    // モノラルの入力（オーディオトラック）は両チャンネルに同じ音がある前提で、そのまま処理する
    dsp.process (channels, numChannels, fc.bufferNumSamples);
}
