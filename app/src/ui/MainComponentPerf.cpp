// 動作の重さを測る（--perf-test <曲のフォルダ>）。CI や手元で、重くなっていないかを数字で確かめる。

#include "MainComponent.h"
#include "MainComponentCommands.h"

#include <fstream>
#include <iostream>

#include <ctime>

namespace
{
    /** 使っているメモリ（MB。Linux だけ。分からなければ -1）。 */
    double residentMegabytes()
    {
        std::ifstream f ("/proc/self/statm");
        long pages = 0, resident = 0;

        if (! (f >> pages >> resident))
            return -1.0;

        return (double) resident * 4096.0 / (1024.0 * 1024.0);
    }
}

void MainComponent::runPerfTest (const juce::File& project, std::function<void()> done)
{
    struct Phase { const char* name; int seconds; bool play, edit, mixer; };
    static const Phase phases[] = { { "idle", 4, false, false, false }, { "play", 6, true, false, false },
                                    { "play+edit", 6, true, true, false }, { "play+mixer", 6, true, false, true } };

    struct Stats
    {
        size_t phase = 0;
        juce::uint32 phaseStart = 0, lastFrame = 0, lastEdit = 0;
        std::clock_t cpuStart = 0;
        int frames = 0, slowFrames = 0;
        juce::uint32 maxGap = 0;
    };

    auto stats = std::make_shared<Stats>();

    // SHAREDAW_PERF_PHASE=<名前> でその段階だけを SHAREDAW_PERF_SECONDS 秒（呼び出しの履歴を取って調べるとき用）
    const auto only = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_PERF_PHASE", {});
    const int onlySeconds = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_PERF_SECONDS", "60").getIntValue();

    while (only.isNotEmpty() && stats->phase + 1 < std::size (phases) && only != phases[stats->phase].name)
        ++stats->phase;

    const auto openStart = juce::Time::getMillisecondCounterHiRes();
    openProjectFolder (project);
    std::cout << "perf open: " << juce::String (juce::Time::getMillisecondCounterHiRes() - openStart, 0) << " ms, memory "
              << juce::String (residentMegabytes(), 0) << " MB" << std::endl;

    // 最初の MIDI クリップを選んで、下のピアノロールに出す
    for (auto& t : document.getProject().tracks)
        if (! t.midiClips.empty())
        {
            state.selectedTrackId = t.id;
            state.selectClip (t.midiClips.front().id);
            break;
        }

    state.autoScroll = true;
    state.changed();

    auto startPhase = [this, stats]
    {
        const auto& p = phases[stats->phase];
        stats->phaseStart = stats->lastFrame = juce::Time::getMillisecondCounter();
        stats->cpuStart = std::clock();
        stats->frames = stats->slowFrames = 0;
        stats->maxGap = 0;

        if (p.play && ! bridge.isPlaying())
            commandManager.invokeDirectly (MainCommands::cmdPlay, false);

        if (p.mixer != (mixerWindow != nullptr))
            toggleMixer();
    };

    startPhase();

    perfFrame = [this, stats, startPhase, done, only, onlySeconds]
    {
        const auto now = juce::Time::getMillisecondCounter();
        const auto gap = now - stats->lastFrame;
        stats->lastFrame = now;
        ++stats->frames;
        stats->maxGap = juce::jmax (stats->maxGap, gap);
        stats->slowFrames += gap > 50 ? 1 : 0;

        const auto& p = phases[stats->phase];

        // 編集: 選んでいるクリップのノートの強さを 1 秒に 10 回変える（ドラッグで動かしているときと同じくらい）
        if (p.edit && now - stats->lastEdit >= 100)
        {
            stats->lastEdit = now;
            const auto clipId = state.selectedClipId;
            const int v = 60 + (int) ((now / 100) % 60);

            document.perform ("perf"_ju, [clipId, v] (collab::Project& pr)
            {
                for (auto& t : pr.tracks)
                    for (auto& c : t.midiClips)
                        if (c.id == clipId && ! c.notes.empty())
                            c.notes.front().velocity = v;
            });   // 1 回ずつ別の操作（元に戻すの履歴が増えていく）
        }

        if (now - stats->phaseStart < (juce::uint32) (only.isNotEmpty() ? onlySeconds : p.seconds) * 1000)
            return;

        const double seconds = (now - stats->phaseStart) / 1000.0;
        const double cpuMs = 1000.0 * (double) (std::clock() - stats->cpuStart) / CLOCKS_PER_SEC;
        std::cout << "perf " << p.name << ": cpu " << juce::String (cpuMs / seconds, 1) << " ms/s, frames "
                  << juce::String (stats->frames / seconds, 1) << "/s, max gap " << stats->maxGap << " ms, slow " << stats->slowFrames
                  << ", memory " << juce::String (residentMegabytes(), 0) << " MB" << std::endl;

        if (only.isEmpty() && ++stats->phase < std::size (phases))
            return startPhase();

        perfFrame = nullptr;
        bridge.stop();

        if (mixerWindow != nullptr)
            toggleMixer();

        juce::MessageManager::callAsync (done);
    };
}
