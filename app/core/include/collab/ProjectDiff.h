#pragma once

// 同期のための差分計算とマージ（仕様書 §4.3〜4.5）。すべて要素の UUID を基準にする。

#include <map>
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
enum class ScopeKind { track, tempo, meter, chord, marker, key, master };

struct Change
{
    enum class Category { track, instrument, notes, clips, audioClips, render, chords, tempo, meter, markers, keys, master };

    std::string scopeId;
    ScopeKind scopeKind = ScopeKind::track;
    std::string scopeName;          // 表示名（トラック名、「テンポ」など）
    Category category = Category::track;
    std::string summary;            // 例: 「3〜4小節目 ノート変更 5件」
    Tick fromTick = -1, toTick = -1; // 該当箇所（ジャンプ・ハイライト用。なければ -1）

    // 変更の中身を特定する（1 件ずつ元に戻す・変更後に戻すため。applyChangeFrom）
    std::string part;               // "name", "volume", "notes", "midiClip", "audioClip", "trackAdded", "scope" など
    std::string itemId;             // クリップの id など（part が midiClip / audioClip のとき）

    /** 同じ変更か（一覧で覚えておくための目印）。 */
    std::string key() const         { return scopeId + "/" + part + "/" + itemId; }
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
// ロックなしの同期（§4.5〜4.6）: スコープ（トラック・テンポなど）ごとに「自分の変更」「サーバーの変更」「競合」を見て、
// 利用者がスコープごとに採用する版を選ぶ。

/** スコープの同期の状態。 */
struct ScopeSyncState
{
    std::string id;
    ScopeKind kind = ScopeKind::track;
    std::string name;          // 表示名（トラック名、「テンポ」など）
    bool mine = false;         // この PC で変わった（ベース → ローカル）
    bool theirs = false;       // サーバーで変わった（ベース → ヘッド）
    bool conflict = false;     // 両方で変わっていて、内容が違う
    bool inLocal = false, inHead = false;
};

/**
    すべてのスコープの状態（ローカルの並び順、ヘッドにだけあるトラックはその後ろ）。
    head が nullptr なら、サーバーの変更はないものとする。
*/
std::vector<ScopeSyncState> syncStates (const Project& base, const Project& local, const Project* head);

/** from の scopeIds のスコープを source の内容に置き換えたもの（source にないトラックは消し、from にないトラックは足す）。 */
Project replaceScopes (const Project& from, const Project& source, const std::set<std::string>& scopeIds);

/** 取り込むときの、スコープごとの採用の選択。 */
enum class Resolution { mine, theirs, both };

/**
    取り込み（ダウンロード）の結果を作る。ヘッドを基に:
    - choices にあるスコープはその選択（both はトラックだけ: サーバーの版に加えて自分の版を別のトラックとして残す）
    - choices にないスコープ: 自分だけが変えた → ローカル、それ以外 → ヘッド
*/
Project resolvePull (const Project& base, const Project& local, const Project& head, const std::map<std::string, Resolution>& choices);

/**
    target のうち change が指す部分（トラックの音量、あるクリップ、ノート、テンポ全体など）だけを source と同じにしたもの。
    source にベース（前回の同期の版）を渡せば、その変更だけを元に戻せる。戻す前の版を渡せば、変更後に戻せる。
*/
Project applyChangeFrom (const Project& target, const Project& source, const Change&);

/** 日本語のドラムパーツ名（差分表示用）。不明なキーはそのまま返す。 */
std::string drumPieceDisplayName (const std::string& key);

} // namespace collab
