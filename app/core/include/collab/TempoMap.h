#pragma once

#include <vector>

#include "Project.h"

namespace collab
{

struct BarBeat
{
    int bar = 1;          // 1 始まり
    int beat = 1;         // 1 始まり（拍子の分母の音符を 1 拍とする）
    Tick tickInBeat = 0;
};

struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;

    Tick ticksPerBeat() const noexcept    { return (Tick) kPpq * 4 / denominator; }
    Tick ticksPerBar() const noexcept     { return ticksPerBeat() * numerator; }

    bool operator== (const TimeSignature&) const = default;
};

/**
    テンポ（階段状）と拍子から、tick ⇔ 秒、tick ⇔ 小節・拍 を変換する。
    テンポの BPM は四分音符基準（MIDI の慣例）。
*/
class TempoMap
{
public:
    TempoMap();
    TempoMap (const TempoTrack&, const MeterTrack&);
    explicit TempoMap (const Project& p) : TempoMap (p.tempoTrack, p.meterTrack) {}

    double tickToSeconds (double tick) const;
    double secondsToTick (double seconds) const;

    SampleCount tickToSamples (double tick, double sampleRate = kSampleRate) const;
    double samplesToTick (SampleCount samples, double sampleRate = kSampleRate) const;

    double bpmAtTick (Tick) const;

    Tick barToTick (int bar) const;           // 小節の頭の tick
    int tickToBar (Tick) const;               // その tick を含む小節番号
    BarBeat tickToBarBeat (Tick) const;
    TimeSignature timeSignatureAtBar (int bar) const;
    TimeSignature timeSignatureAtTick (Tick) const;

private:
    struct TempoSeg { Tick tick; double bpm; double startSeconds; };
    struct MeterSeg { int bar; Tick startTick; TimeSignature sig; };

    std::vector<TempoSeg> tempos;
    std::vector<MeterSeg> meters;

    void build (std::vector<TempoEvent>, std::vector<MeterEvent>);
};

/** プロジェクトの末尾 tick（MIDI クリップ・オーディオクリップ・コードイベントの最大値）。 */
Tick contentEndTick (const Project&, const TempoMap&);

} // namespace collab
