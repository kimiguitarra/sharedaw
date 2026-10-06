#include "MasterLimiterPlugin.h"

const char* MasterLimiterPlugin::xmlTypeName = "shareDawMasterLimiter";

MasterLimiterPlugin::MasterLimiterPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
    blockScratch.reserve (64);
}

MasterLimiterPlugin::~MasterLimiterPlugin()
{
    notifyListenersOfDeletion();
}

double MasterLimiterPlugin::getLatencySeconds()
{
    return dsp.getLatencySamples() / sampleRate;
}

void MasterLimiterPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    sampleRate = info.sampleRate;
    dsp.setParams (current);
    dsp.prepare (info.sampleRate);
    loudness.prepare (info.sampleRate);
}

void MasterLimiterPlugin::reset()
{
    needsReset = true;
}

void MasterLimiterPlugin::setLimiter (const collab::MasterLimiter& l)
{
    if (l == current)
        return;

    current = l;
    limiterEnabled = l.enabled;
    const juce::SpinLock::ScopedLockType sl (lock);
    pending = l;
    hasPending = true;
}

void MasterLimiterPlugin::applyToBuffer (const te::PluginRenderContext& fc)
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
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int n = fc.bufferNumSamples;

    if (numChannels <= 0 || n <= 0)
        return;

    float* channels[2] = {};

    for (int ch = 0; ch < numChannels; ++ch)
        channels[ch] = buffer.getWritePointer (ch, fc.bufferStartSample);

    auto peakOf = [&]
    {
        float p = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            p = juce::jmax (p, juce::FloatVectorOperations::findMaximum (channels[ch], n),
                            -juce::FloatVectorOperations::findMinimum (channels[ch], n));

        return p;
    };

    auto raise = [] (std::atomic<float>& a, float v)
    {
        float old = a.load();
        while (v > old && ! a.compare_exchange_weak (old, v)) {}
    };

    raise (inputPeak, peakOf());

    if (limiterEnabled.load (std::memory_order_relaxed))
        dsp.process (channels, numChannels, n);
    else
        dsp.processBypassed (channels, numChannels, n);   // 遅れはオンのときと同じ（報告している遅れと合わせる）

    raise (outputPeak, peakOf());

    // イメージャー用（リミッターの後）
    {
        int w = scopeWrite.load (std::memory_order_relaxed);

        for (int i = 0; i < n; ++i)
        {
            scopeLeft[(size_t) w] = channels[0][i];
            scopeRight[(size_t) w] = channels[numChannels > 1 ? 1 : 0][i];
            w = (w + 1) % scopeSize;
        }

        scopeWrite.store (w, std::memory_order_release);
    }

    // ラウドネス（リミッターの後）
    blockScratch.clear();
    loudness.process (channels, numChannels, n, blockScratch);

    for (double b : blockScratch)
    {
        const auto scope = fifo.write (1);

        if (scope.blockSize1 > 0)
            fifoData[(size_t) scope.startIndex1] = b;
    }
}

bool MasterLimiterPlugin::getLatestStereo (float* left, float* right, int numSamples) const
{
    if (numSamples > scopeSize)
        return false;

    const int w = scopeWrite.load (std::memory_order_acquire);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto k = (size_t) ((w - numSamples + i + scopeSize) % scopeSize);
        left[i] = scopeLeft[k];
        right[i] = scopeRight[k];
    }

    return true;
}

void MasterLimiterPlugin::takeLoudnessBlocks (std::vector<double>& out)
{
    const auto scope = fifo.read (fifo.getNumReady());

    for (int i = 0; i < scope.blockSize1; ++i)
        out.push_back (fifoData[(size_t) (scope.startIndex1 + i)]);

    for (int i = 0; i < scope.blockSize2; ++i)
        out.push_back (fifoData[(size_t) (scope.startIndex2 + i)]);
}
