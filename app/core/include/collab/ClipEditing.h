#pragma once

// クリップの非破壊編集（§3.6）。元ファイルは書き換えず、クリップのパラメータだけを変える。

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "Project.h"
#include "TempoMap.h"

namespace collab
{

/** オーディオクリップの終わりの位置（テンポに追従しないので、秒 → tick で求める）。 */
Tick audioClipEndTick (const AudioClip&, const TempoMap&);

/** クリップの開始位置 startTick からの秒数をサンプル数にしたもの。 */
SampleCount samplesBetween (Tick from, Tick to, const TempoMap&);

/**
    tick の位置で分割する。位置がクリップの内側でなければ std::nullopt。
    右側のクリップには newId が付く。フェードは外側だけ残す。
*/
std::optional<std::pair<AudioClip, AudioClip>> splitAudioClip (const AudioClip&, Tick at, const TempoMap&, const std::string& newId);

/** MIDI クリップを分割する。分割位置より後に始まるノートは右側へ移り、左側のノートは分割位置で切る。 */
std::optional<std::pair<MidiClip, MidiClip>> splitMidiClip (const MidiClip&, Tick at, const std::string& newId,
                                                            const std::function<std::string()>& makeNoteId);

/**
    開始位置を newStart に動かす（終わりの位置は変えない）。元ファイルの範囲を超えないよう制限する。
    sourceLength は実体の長さ（サンプル）。
*/
AudioClip trimAudioClipStart (const AudioClip&, Tick newStart, const TempoMap&);

/** 長さを変える（開始位置は変えない）。newEnd は tick、sourceLength を超えない。 */
AudioClip trimAudioClipEnd (const AudioClip&, Tick newEnd, SampleCount sourceLength, const TempoMap&);

/**
    MIDI クリップの開始位置を newStart に動かす（終わりの位置は変えない）。ノートの位置（絶対）は変えないので、
    縮めて外に出たノートは消えずに隠れ、伸ばし直すとまた見える（Cubase と同じ）。
*/
MidiClip trimMidiClipStart (const MidiClip&, Tick newStart, Tick minLength);

/** 2 つの MIDI クリップを 1 つにする（a の先頭から b の終わりまで。b のノートは a の中へ移す）。 */
MidiClip glueMidiClips (const MidiClip& a, const MidiClip& b);

/** 元ファイルで続いている 2 つのオーディオクリップを 1 つにする（続いていなければ nullopt）。 */
std::optional<AudioClip> glueAudioClips (const AudioClip& a, const AudioClip& b, const TempoMap&);

/**
    重なったオーディオクリップのうち、実際に鳴る部分（Pro Tools と同じく、後ろにある（新しい）クリップが上になり、
    下のクリップの重なった所は鳴らない）。下のクリップのデータはそのままなので、上のクリップを動かせばまた鳴る。
    上と下の切り替わりはクロスフェード（crossfadeSeconds の間、下を少し残して両方を等パワーで入れ替える）。
*/
struct AudibleSegment
{
    size_t clipIndex = 0;
    double startSeconds = 0, lengthSeconds = 0;
    double offsetSeconds = 0;              // 元ファイルの読み始め
    double fadeInSeconds = 0, fadeOutSeconds = 0;
    bool crossfadeIn = false, crossfadeOut = false;   // そのフェードがクロスフェード（等パワーの形にする）か
};

inline constexpr double defaultCrossfadeSeconds = 0.010;

std::vector<AudibleSegment> audibleSegments (const std::vector<AudioClip>&, const TempoMap&,
                                            double crossfadeSeconds = defaultCrossfadeSeconds);

} // namespace collab
