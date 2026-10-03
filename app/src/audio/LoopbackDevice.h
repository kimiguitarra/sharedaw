#pragma once

// 録音のタイミングを確かめるための仮想のオーディオ機器（--record-test で SHAREDAW_LOOPBACK を指定したときだけ使う）。
//
// 出力の 1 チャンネル目を、報告している遅れ（出力 + 入力のレイテンシ。バッファの長さ以上にする）の分だけ遅らせて入力に戻す。
// つまり「出力から鳴った音を、ゼロレイテンシーでモニターしている人が聞いて、そのまま弾いた」のと同じ入力になる。
// 正しく補正できていれば、録音した音はクリックと同じ位置に来る。
//
// MIDI も同じく、出力のクリックが「聞こえた」瞬間（出力のレイテンシの後）に onClickHeard を呼ぶ（MIDI を送る用）。

#include "Common.h"
#include <iostream>

class LoopbackDevice  : public juce::AudioIODevice,
                        private juce::Thread
{
public:
    LoopbackDevice (int inputLatency, int outputLatency, int bufferSize)
        : AudioIODevice ("Loopback", "Loopback"), Thread ("loopback"),
          blockSize (bufferSize), inLatency (inputLatency), outLatency (outputLatency)
    {
    }

    ~LoopbackDevice() override   { close(); }

    /** 出力のクリック（無音からの立ち上がり）が聞こえる時刻（Time::getMillisecondCounterHiRes の ms）。別のスレッドから呼ぶ。 */
    std::function<void (double heardAtMs)> onClickHeard;

    juce::StringArray getOutputChannelNames() override       { return { "Out 1", "Out 2" }; }
    juce::StringArray getInputChannelNames() override        { return { "In 1", "In 2" }; }
    juce::Array<double> getAvailableSampleRates() override   { return { rate }; }
    juce::Array<int> getAvailableBufferSizes() override      { return { blockSize }; }
    int getDefaultBufferSize() override                      { return blockSize; }

    juce::String open (const juce::BigInteger& ins, const juce::BigInteger& outs, double, int) override
    {
        activeIns = ins;
        activeOuts = outs;
        opened = true;
        return {};
    }

    void close() override                                    { stop(); opened = false; }
    bool isOpen() override                                   { return opened; }

    void start (juce::AudioIODeviceCallback* cb) override
    {
        if (cb == nullptr || isThreadRunning())
            return;

        callback = cb;
        callback->audioDeviceAboutToStart (this);
        startThread (juce::Thread::Priority::highest);
    }

    void stop() override
    {
        stopThread (2000);

        if (auto* cb = std::exchange (callback, nullptr))
            cb->audioDeviceStopped();
    }

    bool isPlaying() override                                { return callback != nullptr; }
    juce::String getLastError() override                     { return {}; }
    int getCurrentBufferSizeSamples() override               { return blockSize; }
    double getCurrentSampleRate() override                   { return rate; }
    int getCurrentBitDepth() override                        { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOuts; }
    juce::BigInteger getActiveInputChannels() const override  { return activeIns; }
    int getOutputLatencyInSamples() override                 { return outLatency; }
    int getInputLatencyInSamples() override                  { return inLatency; }

private:
    const int blockSize;
    const double rate = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_LOOPBACK_RATE", "48000").getDoubleValue();
    const int inLatency, outLatency;
    juce::BigInteger activeIns, activeOuts;
    bool opened = false;
    juce::AudioIODeviceCallback* callback = nullptr;

    void run() override
    {
        const int delay = inLatency + outLatency;
        std::vector<float> line ((size_t) (delay + blockSize * 4), 0.0f);   // 出力 1 の履歴（輪）
        juce::int64 written = 0;
        const int numIns = activeIns.countNumberOfSetBits(), numOuts = activeOuts.countNumberOfSetBits();
        juce::AudioBuffer<float> in (juce::jmax (1, numIns), blockSize), out (juce::jmax (1, numOuts), blockSize);
        const double start = juce::Time::getMillisecondCounterHiRes();
        juce::int64 block = 0;
        float previous = 0.0f;
        int quiet = blockSize * 100;

        while (! threadShouldExit())
        {
            // 入力 = delay サンプル前の出力（その前は無音）
            for (int i = 0; i < blockSize; ++i)
            {
                const auto n = written + i - delay;
                const float v = n >= 0 ? line[(size_t) (n % (juce::int64) line.size())] : 0.0f;

                for (int ch = 0; ch < in.getNumChannels(); ++ch)
                    in.setSample (ch, i, v);
            }

            out.clear();
            const double blockTime = juce::Time::getMillisecondCounterHiRes();
            juce::AudioIODeviceCallbackContext context;
            callback->audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), numIns, out.getArrayOfWritePointers(), numOuts, blockSize, context);

            for (int i = 0; i < blockSize; ++i)
            {
                const float v = numOuts > 0 ? out.getSample (0, i) : 0.0f;
                line[(size_t) ((written + i) % (juce::int64) line.size())] = v;

                // クリックの立ち上がり（しばらく無音の後に大きくなった所）が聞こえる時刻を知らせる
                if (std::abs (v) > 0.02f && std::abs (previous) <= 0.02f && quiet > (int) (rate * 0.1) && onClickHeard)
                {
                    if (juce::SystemStats::getEnvironmentVariable ("SHAREDAW_LOOPBACK_LOG", {}).isNotEmpty())
                        std::cerr << "loopback click at output sample " << (written + i) << " block " << block << " offset " << i << std::endl;

                    onClickHeard (blockTime + (i + outLatency) * 1000.0 / rate);
                }

                quiet = std::abs (v) > 0.02f ? 0 : quiet + 1;
                previous = v;
            }

            written += blockSize;
            ++block;

            // 実時間で進める（MIDI の時刻は実時間なので）
            const double next = start + (double) block * blockSize * 1000.0 / rate;
            const double wait = next - juce::Time::getMillisecondCounterHiRes();

            if (wait > 0)
                juce::Thread::sleep ((int) wait);
        }
    }
};

class LoopbackDeviceType  : public juce::AudioIODeviceType
{
public:
    LoopbackDeviceType (int in, int out, int bufferSize) : AudioIODeviceType ("Loopback"), inLatency (in), outLatency (out), blockSize (bufferSize) {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override   { return { "Loopback" }; }
    int getDefaultDeviceIndex (bool) const override          { return 0; }
    int getIndexOfDevice (juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override        { return false; }

    juce::AudioIODevice* createDevice (const juce::String&, const juce::String&) override
    {
        auto* d = new LoopbackDevice (inLatency, outLatency, blockSize);
        d->onClickHeard = [this] (double ms) { if (onClickHeard) onClickHeard (ms); };
        return d;
    }

    std::function<void (double heardAtMs)> onClickHeard;

private:
    const int inLatency, outLatency, blockSize;
};
