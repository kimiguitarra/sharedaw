#pragma once

#include "EngineBridge.h"

/** 録音したテイクの取り込み（§3.5: 録音確定時に 48kHz / 32bit float WAV に変換し、ハッシュ名にする）。 */
namespace Takes
{
    struct Clip
    {
        std::string trackId;
        collab::AudioClip clip;
    };

    /** テイクを audio/ に取り込んでクリップを作る。元のファイルは消す。バックグラウンドスレッドから呼べる。 */
    juce::Result import (const std::vector<EngineBridge::RecordedTake>&, const juce::File& projectDir,
                         const collab::TempoMap&, std::vector<Clip>& result);

    /** クリップをプロジェクトに追加する（メッセージスレッド、1 回の操作として元に戻せる）。 */
    void addToProject (ProjectDocument&, const std::vector<Clip>&);
}
