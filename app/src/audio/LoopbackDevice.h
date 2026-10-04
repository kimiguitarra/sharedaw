#pragma once

// 録音のタイミングを確かめるための仮想のオーディオ機器（--record-test で SHAREDAW_LOOPBACK を指定したときだけ使う）。
//
// 出力の 1 チャンネル目を、報告している遅れ（出力 + 入力のレイテンシ。バッファの長さ以上にする）の分だけ遅らせて入力に戻す。
// つまり「出力から鳴った音を、ゼロレイテンシーでモニターしている人が聞いて、そのまま弾いた」のと同じ入力になる。
// 正しく補正できていれば、録音した音はクリックと同じ位置に来る。
//
// MIDI も同じく、出力のクリックが「聞こえた」瞬間（出力のレイテンシの後）に onClickHeard を呼ぶ（MIDI を送る用）。

#include "Common.h"
#include <atomic>
#include <iostream>

#if JUCE_LINUX
 #include <csignal>
 #include <execinfo.h>
 #include <pthread.h>
 #include <unistd.h>
 #include <sys/syscall.h>
 #include <dirent.h>

// SHAREDAW_LOOPBACK_STACKS があれば、処理が 8 ms を超えたときに全スレッドのスタックを出す（重い所を探す用）
namespace LoopbackStacks
{
    inline std::atomic_flag busy = ATOMIC_FLAG_INIT;

    inline void onSignal (int)
    {
        while (busy.test_and_set()) {}

        void* frames[64];
        const int n = backtrace (frames, 64);
        char header[96];
        const int len = snprintf (header, sizeof (header), "---- thread %ld stack ----\n", (long) syscall (SYS_gettid));
        [[maybe_unused]] auto w = write (2, header, (size_t) len);
        backtrace_symbols_fd (frames, n, 2);
        busy.clear();
    }

    /** プロセスのすべてのスレッドのスタックを出す。 */
    inline void dumpAllThreads()
    {
        if (auto* dir = opendir ("/proc/self/task"))
        {
            while (auto* e = readdir (dir))
                if (e->d_name[0] != '.')
                    syscall (SYS_tgkill, getpid(), atoi (e->d_name), SIGPROF);

            closedir (dir);
        }
    }
}
#endif

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

    /** オーディオの処理（コールバック）にかかった時間。バッファの長さを超えると、本物の機器では音が途切れる（プツっと鳴る）。 */
    struct Timing
    {
        std::atomic<int> callbacks { 0 }, overBudget { 0 };
        std::atomic<double> slowestMs { 0.0 };
    };

    static Timing& timing()   { static Timing t; return t; }

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
    std::atomic<double> callbackStartMs { 0.0 };

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

       #if JUCE_LINUX
        std::unique_ptr<std::thread> watchdog;
        std::atomic<bool> stopWatchdog { false };

        if (juce::SystemStats::getEnvironmentVariable ("SHAREDAW_LOOPBACK_STACKS", {}).isNotEmpty())
        {
            std::signal (SIGPROF, LoopbackStacks::onSignal);
            std::cerr << "loopback: maps " << juce::File ("/proc/self/maps").loadFileAsString().upToFirstOccurrenceOf ("\n", false, false) << std::endl;
            watchdog = std::make_unique<std::thread> ([this, &stopWatchdog]
            {
                double reported = 0.0;

                while (! stopWatchdog)
                {
                    std::this_thread::sleep_for (std::chrono::milliseconds (3));
                    const double started = callbackStartMs.load();

                    if (started > 0.0 && started != reported && juce::Time::getMillisecondCounterHiRes() - started > 8.0)
                    {
                        reported = started;
                        std::cerr << "---- overrun: all threads ----" << std::endl;
                        LoopbackStacks::dumpAllThreads();
                    }
                }
            });
        }

        const juce::ScopeGuard joinWatchdog { [&] { stopWatchdog = true; if (watchdog) watchdog->join(); } };
       #endif

        // SHAREDAW_LOOPBACK_CAPTURE=<wav> なら、出力をそのまま書き出す（再生中のノイズの確認用: --switch-test）
        std::unique_ptr<juce::AudioFormatWriter> capture;

        if (auto path = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_LOOPBACK_CAPTURE", {}); path.isNotEmpty())
        {
            juce::File file (path);
            file.deleteFile();

            if (auto stream = file.createOutputStream())
            {
                juce::WavAudioFormat wav;
                capture.reset (wav.createWriterFor (stream.release(), rate, (unsigned int) juce::jmax (1, numOuts), 32, {}, 0));
            }
        }

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
            callbackStartMs = blockTime;
            juce::AudioIODeviceCallbackContext context;
            callback->audioDeviceIOCallbackWithContext (in.getArrayOfReadPointers(), numIns, out.getArrayOfWritePointers(), numOuts, blockSize, context);

            callbackStartMs = 0.0;

            {
                const double took = juce::Time::getMillisecondCounterHiRes() - blockTime;
                const double budget = blockSize * 1000.0 / rate;
                auto& t = timing();
                ++t.callbacks;

                if (took > t.slowestMs.load())
                    t.slowestMs = took;

                if (took > budget)
                {
                    ++t.overBudget;
                    std::cerr << "loopback: callback took " << took << " ms (budget " << budget << " ms) at block " << block << std::endl;
                }
            }

            if (capture != nullptr)
                capture->writeFromAudioSampleBuffer (out, 0, blockSize);

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
