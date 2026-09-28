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
    juce::Result runRegister (const collab::Project& snapshot, const juce::File& projectDir, const SyncProgress& progress = {});
    void applyRegistered (const collab::Project& snapshot, int revision);

    // pull（§4.5）
    struct PullPreview
    {
        int head = 0;
        collab::Project headProject;
        collab::ProjectDiff diff;   // ベース → ヘッド
    };

    juce::Result fetchPullPreview (PullPreview&);

    //==============================================================================
    // サーバーの状況（同期パネル・ツールバーの表示用）。リンク中は定期的にバックグラウンドで確認する。
    struct RevisionInfo
    {
        int number = 0;
        std::string authorId;
        juce::String author, message;
        juce::Time createdAt;
    };

    struct ServerStatus
    {
        bool checked = false;                         // 一度でも確認した
        bool checking = false;
        bool online = false;
        juce::String error;
        juce::Time checkedAt;
        int head = 0;
        int base = 0;                                 // 確認したときのベース
        std::vector<RevisionInfo> incoming;           // ベースより新しいリビジョン（新しい順）
        std::vector<RevisionInfo> history;            // 最近のリビジョン（新しい順）
        std::shared_ptr<const PullPreview> preview;   // ヘッドの内容とベースからの差分（head > base のとき）
    };

    ServerStatus getServerStatus() const;

    /** すぐにサーバーを確認する（バックグラウンド。終わったら変更通知）。 */
    void checkServerNow();

    /** 他の人の新しいリビジョンを見つけたとき（メッセージスレッド。同じリビジョンは 1 回だけ）。 */
    std::function<void (const std::vector<RevisionInfo>&)> onIncomingRevisions;

    /** ロックの一覧（同期パネル用）。 */
    std::map<std::string, LockInfo> getLocks() const;
    juce::Result runDownloadAudio (const collab::Project&, const juce::File& projectDir, const SyncProgress& progress = {});

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
    juce::Result runPush (const PushPlan&, const juce::String& message, bool releaseLocks, const juce::File& projectDir, int& newRevision,
                         const SyncProgress& progress = {});
    void applyPushed (const PushPlan&, int newRevision);

    // サーバーから開く
    juce::Result fetchProjects (nlohmann::json& list);
    juce::Result runOpenFromServer (const std::string& projectId, const juce::File& parentDir, juce::File& createdFolder,
                                   const SyncProgress& progress = {});

    juce::Result fetchRevisions (nlohmann::json& list);

    /**
        サーバーから曲を削除する（作った人だけ）。成功したら、この PC のフォルダ（あれば）はサーバーとのつながりを外す
        （フォルダと曲の中身は残る。「この PC だけ」の曲になる）。
    */
    juce::Result runDeleteProject (const std::string& projectId, const juce::File& localFolder);

    /** ローカルのプロジェクトのフォルダの状態（開かずに読む。「楽曲を選ぶ」画面用）。 */
    struct LocalInfo
    {
        juce::File folder;
        bool valid = false;               // project.json が読めた
        std::string projectId;
        juce::String name;
        bool linked = false;              // .collab/meta.json がある（サーバーに登録済み）
        juce::String serverUrl;
        int baseRevision = 0;
        int changedScopes = 0;            // ベースから変わっているスコープ（トラック・テンポなど）の数 = 未 push の変更
        juce::StringArray changedNames;   // その名前（最大 5 件）
        juce::Time savedAt;
    };

    static LocalInfo inspectFolder (const juce::File& projectFolder);

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

    ServerStatus serverStatus;
    juce::CriticalSection statusLock;
    int notifiedHead = 0;   // onIncomingRevisions で知らせた最新のリビジョン

    /** サーバーの状況（ヘッド・履歴・ロック・取り込む変更）をバックグラウンドで更新する（開いたとき・定期的に）。 */
    void refreshInBackground();
    void timerCallback() override      { refreshInBackground(); }

    /** ヘッドのプロジェクトをダウンロードして、ベースからの差分を作る。 */
    static juce::Result buildPreview (const SyncClient&, const std::string& projectId, int head,
                                      const std::optional<collab::Project>& base, PullPreview&);

    bool guardEdit (const collab::Project& before, const collab::Project& after);
    void saveMeta (const juce::File& projectDir) const;
    static void saveBase (const juce::File& projectDir, const collab::Project&);

    juce::Result uploadMissingBlobs (const SyncClient&, const std::vector<std::string>& hashes, const juce::File& projectDir,
                                     const std::map<std::string, std::string>& inlineContent, const SyncProgress& progress);

    /** 署名付き URL（R2 へ直接）のアップロードが失敗したら、以後はサーバー経由で送る。 */
    std::atomic<bool> directUploadBroken { false };

    JUCE_DECLARE_NON_COPYABLE (SyncManager)
};
