#pragma once

#include "Common.h"
#include "collab/Project.h"

class EngineBridge;
class ProjectDocument;

/**
    書き出し（ファイル → 書き出し）。画面とコマンドライン（--export-*、CI の確認）の両方から使う。
    どれも曲の頭から最後まで（リバーブなどの余韻が消えるまで）。メトロノームとこの PC だけのマスター音量は含めない。
*/
namespace Export
{
    /** 書き出す範囲（tick。左右のロケーターなど）。渡さなければ曲全体（頭から最後の音の余韻が消えるまで）。範囲では余韻を足さない。 */
    struct Range
    {
        collab::Tick start = 0, end = 0;
    };

    /** ミックスダウン（48 kHz / 24 bit WAV。マスターのリミッターを含む）。 */
    juce::Result mixdownWav (EngineBridge&, const ProjectDocument&, const juce::File& wav, const Range* range = nullptr);

    /** ミックスダウン（44.1 kHz / 320 kbps MP3）。 */
    /** runEncode があれば、MP3 への変換をそれに渡して行う（画面では別スレッドで、進み具合を出して）。 */
    juce::Result mixdownMp3 (EngineBridge&, const ProjectDocument&, const juce::File& mp3,
                             std::function<juce::Result (std::function<juce::Result()>)> runEncode = {}, const Range* range = nullptr);

    /**
        パラデータ: トラックごとの WAV（48 kHz / 24 bit）を folder に書く。ミックスで聞こえるとおり（インサート・EQ・Comp・音量・パン）で、
        センド・バス・マスターは通さない。ミュート中のトラックとバスは書かない。コードを鳴らしていれば「コード」も書く。
        全部のファイルは同じ長さ（頭をそろえて DAW に並べればそのまま合う）。written に書いたファイルを入れる。
    */
    juce::Result stems (EngineBridge&, const ProjectDocument&, const juce::File& folder, juce::Array<juce::File>& written,
                        const Range* range = nullptr);

    /** MIDI ファイル（SMF タイプ 1。MIDI トラックとコードトラック、テンポ・拍子・キー・マーカー）。 */
    juce::Result midi (const ProjectDocument&, const juce::File& mid);

    /** 書き出した音のラウドネス（LUFS）とピークの説明（読めなければ空）。 */
    juce::String loudnessSummary (const juce::File& audio);
}
