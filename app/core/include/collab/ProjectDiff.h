#pragma once

// 同期のための差分計算とマージ（仕様書 §4.3〜4.5）。すべて要素の UUID を基準にする。

#include <set>
#include <string>
#include <vector>

#include "Project.h"

namespace collab
{

/**
    差分の単位（スコープ）: トラック、またはテンポトラック・拍子トラック・コードトラック。
    ロックもこの単位で取る（§4.2）。スコープ ID はそれぞれの要素の id。
*/
enum class ScopeKind { track, tempo, meter, chord, marker };

struct Change
{
    enum class Category { track, instrument, notes, clips, audioClips, render, chords, tempo, meter, markers };

    std::string scopeId;
    ScopeKind scopeKind = ScopeKind::track;
    std::string scopeName;          // 表示名（トラック名、「テンポ」など）
    Category category = Category::track;
    std::string summary;            // 例: 「3〜4小節目 ノート変更 5件」
    Tick fromTick = -1, toTick = -1; // 該当箇所（ジャンプ・ハイライト用。なければ -1）
};

struct ProjectDiff
{
    std::vector<Change> changes;
    std::vector<std::string> changedScopeIds;   // 変更のあったスコープ（プロジェクトの並び順）

    bool empty() const noexcept     { return changes.empty(); }
    bool touches (const std::string& scopeId) const;
    std::vector<Change> forScope (const std::string& scopeId) const;
};

/** before → after の差分（pull 前は ベース→ヘッド、push 前は ベース→ローカル）。 */
ProjectDiff diffProjects (const Project& before, const Project& after);

/** スコープ単位で内容が等しいか（トラック・テンポ・拍子・コード）。 */
bool scopeEquals (const Project& a, const Project& b, const std::string& scopeId);

/** 全スコープの ID（テンポ・拍子・コード・各トラック）。 */
std::vector<std::string> allScopeIds (const Project&);

//==============================================================================
struct PullResult
{
    Project merged;
    std::vector<std::string> keptLocalScopes;        // ローカルを維持したスコープ
    std::vector<std::string> conflictScopes;         // 不整合を検知したスコープ（ローカルを競合コピーに退避）
    std::vector<std::string> conflictCopyTrackIds;   // 追加した「競合コピー」トラック
};

/**
    pull（取り込み）のマージ（§4.5）:
    - 自分がロックしていて、ローカルに未 push の変更があるスコープ（新規作成したトラックを含む）: ローカルを維持
    - それ以外: ヘッドを採用
    - ロックを持たないのにローカルが変わっていた場合（本来起きない）: ヘッドを採用し、
      トラックならローカル側を「（競合コピー）」トラックとして残す
*/
PullResult mergeForPull (const Project& base, const Project& local, const Project& head,
                         const std::set<std::string>& lockedByMe);

/** 日本語のドラムパーツ名（差分表示用）。不明なキーはそのまま返す。 */
std::string drumPieceDisplayName (const std::string& key);

} // namespace collab
