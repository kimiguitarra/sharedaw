#pragma once

#include <map>
#include <optional>

#include "ProjectDocument.h"
#include "SyncClient.h"
#include "collab/ProjectDiff.h"

/**
    プロジェクトの同期（仕様書 §4, §5, §6）。

    ローカルの状態は .collab/ に置く:
      meta.json  サーバー URL、プロジェクト ID、ベースリビジョン番号、ユーザー情報
      base.json  ベースリビジョンの project.json（差分計算・pull のマージ用）

    ネットワークを使う処理（名前が "fetch" / "run" で始まるもの）はバックグラウンドスレッドから呼ぶ。
    ProjectDocument を書き換える処理（apply*）はメッセージスレッドから呼ぶ。
*/
class SyncManager  : public juce::ChangeBroadcaster,
                     private juce::Timer
{
public:
    struct Meta
    {
        juce::String serverUrl;
        std::string projectId;
        int baseRevision = 0;
        std::string userId;
        juce::String userName;
    };

    struct LockInfo
    {
        std::string userId;
        juce::String displayName;
    };

    SyncManager (ProjectDocument&, juce::PropertiesFile& settings);
    ~SyncManager() override;

    //==============================================================================
    juce::String getServerUrl() const;
    bool hasCredentials() const;
    void setCredentials (const juce::String& serverUrl, const juce::String& token);
    SyncClient makeClient() const;

    /** GET /me（接続テスト）。成功すればユーザー情報を覚える。 */
    ApiResponse fetchMe();

    //==============================================================================
    /** このプロジェクトがサーバーに登録済み（.collab/meta.json がある）か。 */
    bool isLinked() const noexcept                          { return linked; }
    const Meta& getMeta() const noexcept                    { return meta; }
    const collab::Project* getBase() const noexcept         { return base ? &*base : nullptr; }

    /** ベースから変わっているスコープか（トラックヘッダーの「変更あり」マーク用）。 */
    bool hasLocalChanges (const std::string& scopeId) const;

    //==============================================================================
    // ロック（§4.2）
    bool isLockedByMe (const std::string& scopeId) const;
    std::optional<LockInfo> getLock (const std::string& scopeId) const;

    /** ロックなしで編集してよいスコープか（未登録・ロック保持・新規作成）。 */
    bool canEdit (const std::string& scopeId) const;

    juce::Result fetchLocks();
    juce::Result runAcquireLock (const std::string& scopeId);
    juce::Result runReleaseLock (const std::string& scopeId, bool force);

    /** 編集がロックのないスコープに触れたときに呼ばれる（メッセージスレッド）。 */
    std::function<void (std::vector<std::string> scopeIds)> onLockRequired;

    //==============================================================================
    // サーバーへの登録（新規作成 + 最初の push）
    juce::Result runRegister (const collab::Project& snapshot, const juce::File& projectDir);
    void applyRegistered (const collab::Project& snapshot, int revision);

    // pull（§4.5）
    struct PullPreview
    {
        int head = 0;
        collab::Project headProject;
        collab::ProjectDiff diff;   // ベース → ヘッド
    };

    juce::Result fetchPullPreview (PullPreview&);
    juce::Result runDownloadAudio (const collab::Project&, const juce::File& projectDir);

    struct PullReport
    {
        std::vector<std::string> keptLocal, conflicts;
        juce::File conflictBackup;
    };

    PullReport applyPull (const PullPreview&);

    // push（§4.6）
    struct PushPlan
    {
        int head = 0;
        collab::Project snapshot;
        collab::ProjectDiff diff;                // ベース → ローカル
        std::vector<std::string> notLocked;      // ロックが必要なのに持っていないスコープ
        std::vector<std::string> staleRenders;   // バウンスが必要・古いトラック
        bool needsPull = false;
    };

    juce::Result fetchPushPlan (const collab::Project& snapshot, PushPlan&);
    juce::Result runPush (const PushPlan&, const juce::String& message, bool releaseLocks, const juce::File& projectDir, int& newRevision);
    void applyPushed (const PushPlan&, int newRevision);

    // サーバーから開く
    juce::Result fetchProjects (nlohmann::json& list);
    juce::Result runOpenFromServer (const std::string& projectId, const juce::File& parentDir, juce::File& createdFolder);

    juce::Result fetchRevisions (nlohmann::json& list);

    /** スコープの表示名（トラック名、「テンポ」など）。 */
    juce::String scopeName (const std::string& scopeId) const;

    /** プロジェクトのフォルダが変わったとき（開く・保存）に .collab を読み直す。 */
    void reloadForDocument();

private:
    ProjectDocument& document;
    juce::PropertiesFile& settings;

    bool linked = false;
    Meta meta;
    std::optional<collab::Project> base;
    std::map<std::string, LockInfo> locks;
    juce::CriticalSection lockMapLock;

    std::atomic<bool> refreshing { false };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    /** ロックの状態をバックグラウンドで更新する（開いたとき・定期的に）。 */
    void refreshLocksInBackground();
    void timerCallback() override      { refreshLocksInBackground(); }

    bool guardEdit (const collab::Project& before, const collab::Project& after);
    void saveMeta (const juce::File& projectDir) const;
    static void saveBase (const juce::File& projectDir, const collab::Project&);

    juce::Result uploadMissingBlobs (const SyncClient&, const std::vector<std::string>& hashes, const juce::File& projectDir,
                                     const std::map<std::string, std::string>& inlineContent);

    JUCE_DECLARE_NON_COPYABLE (SyncManager)
};
