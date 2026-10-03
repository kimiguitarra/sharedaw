#pragma once

// バウンス（§3.7）の「バウンスが古い」判定。

#include <functional>
#include <string>

#include "Project.h"

namespace collab
{

/** 外部プラグイン（音源またはエフェクト）を使うトラックか。push 前にバウンスが必須。 */
bool usesExternalPlugin (const Track&);

/**
    バウンスした時点のトラック内容のハッシュ（render.sourceFingerprint）。
    MIDI・オーディオクリップ、音源（内蔵音源のパラメータ、外部プラグインとその状態）、エフェクトを含む。
    名前・色・音量・パン・ミュート・ソロは含めない（バウンス結果に影響しないため）。
    stateHash は stateRef（plugins-state/...）の内容のハッシュを返す関数。
*/
std::string trackSourceFingerprint (const Track&, const std::function<std::string (const std::string& stateRef)>& stateHash);

enum class RenderStatus
{
    notNeeded,   // 外部プラグインを使っていない
    missing,     // バウンスしていない
    stale,       // バウンス後に内容が変わった
    upToDate
};

RenderStatus renderStatus (const Track&, const std::string& currentFingerprint);

/**
    source をバウンスしたオーディオトラック（クリップが source.render の音を使うオーディオトラック）。なければ nullptr。
    バウンスしたトラックはふつうのオーディオトラックとしてアップする（元のトラックが外部プラグインなら、元はこの PC だけに残る）。
*/
const Track* findBounceTrack (const Project&, const Track& source);

/** audioTrack がバウンスで作ったトラックなら、その元のトラック。なければ nullptr。 */
const Track* findBounceSource (const Project&, const Track& audioTrack);

/**
    バウンスの結果をプロジェクトに入れる（Cubase の「インプレイスレンダリング」と同じ）:
    - 元のトラックの render を更新してミュートする（バウンスしたトラックと二重に鳴らないように）
    - バウンスしたオーディオトラックがあればクリップを差し替え（音量などの設定はそのまま）、
      なければ元のトラックのすぐ下に作る（名前は「元の名前（バウンス）」、音量・パン・EQ・コンプ・出力先・センドは元と同じ）
    バウンスしたオーディオトラックの ID を返す。
*/
std::string applyBounce (Project&, const std::string& sourceId, const Render&, SampleCount lengthSamples,
                         const std::string& newTrackId, const std::string& newClipId);

} // namespace collab
