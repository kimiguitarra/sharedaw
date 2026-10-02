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

    /** すべてのスコープ（トラック・テンポなど）の同期の状態（この PC の変更・サーバーの変更・競合）。 */
    std::vector<collab::ScopeSyncState> scopeStates() const;

    /** 1 つのスコープの状態（トラックヘッダーの色分け用。軽い）。 */
    collab::ScopeSyncState scopeState (const std::string& scopeId) const;

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

    /** サーバーの最新（ベースより新しいときだけ。なければ nullptr）。 */
    std::shared_ptr<const PullPreview> headPreview() const;

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

    juce::Result runDownloadAudio (const collab::Project&, const juce::File& projectDir, const SyncProgress& progress = {});

    /**
        ダウンロード（取り込み）。スコープごとの選択（競合など）に従ってヘッドと合わせ、ベースをヘッドにする。
        取り込む前のローカルは .collab/before-download/ に残す。
    */
    void applyDownload (const PullPreview&, const std::map<std::string, collab::Resolution>& choices);

    // アップロード（選んだスコープだけ）
    struct UploadPlan
    {
        int head = 0;
        collab::Project snapshot;                // アップする内容（ベースに選んだスコープのローカルを入れたもの）
        collab::ProjectDiff diff;                // ベース → アップする内容
        std::vector<std::string> staleRenders;   // バウンスが必要・古いトラック
        bool needsDownload = false;              // サーバーに新しい版がある（先にダウンロード）
    };

    juce::Result fetchUploadPlan (const collab::Project& local, const std::set<std::string>& scopeIds, UploadPlan&);
    juce::Result runUpload (const UploadPlan&, const juce::String& message, const juce::File& projectDir, int& newRevision,
                            const SyncProgress& progress = {});
    void applyUploaded (const UploadPlan&, int newRevision);

    /** サーバーの曲の名前を変える（開いている曲なら、曲の名前も変える）。 */
    juce::Result runRenameProject (const std::string& projectId, const juce::String& newName);

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

    std::atomic<bool> refreshing { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);   // バックグラウンドの処理から見る

    ServerStatus serverStatus;
    juce::CriticalSection statusLock;
    int notifiedHead = 0;   // onIncomingRevisions で知らせた最新のリビジョン

    /** サーバーの状況（ヘッド・履歴・ロック・取り込む変更）をバックグラウンドで更新する（開いたとき・定期的に）。 */
    void refreshInBackground();
    void timerCallback() override      { refreshInBackground(); }

    /** ヘッドのプロジェクトをダウンロードして、ベースからの差分を作る。 */
    /** サーバーのリビジョンのプロジェクト JSON を取ってきて読む（ハッシュが合わなければ失敗）。 */
    static juce::Result downloadRevision (const SyncClient&, const std::string& projectId, int revision, collab::Project& result);
    static juce::Result buildPreview (const SyncClient&, const std::string& projectId, int head,
                                      const std::optional<collab::Project>& base, PullPreview&);

    void saveMeta (const juce::File& projectDir) const   { writeMeta (projectDir, meta); }

    /** projectDir/.collab/meta.json を読む（なければ・読めなければ nullopt）。 */
    static std::optional<Meta> readMeta (const juce::File& projectDir);

    /** 変わった所（トラック・テンポなど）の表示名。markRemoved なら、ベースにだけあるトラックに「（削除）」を付ける。 */
    static juce::String scopeDisplayName (const collab::Project&, const collab::Project* base, const std::string& scopeId, bool markRemoved);
    static void writeMeta (const juce::File& projectDir, const Meta&);
    static void saveBase (const juce::File& projectDir, const collab::Project&);

    juce::Result uploadMissingBlobs (const SyncClient&, const std::vector<std::string>& hashes, const juce::File& projectDir,
                                     const std::map<std::string, std::string>& inlineContent, const SyncProgress& progress);

    /** 署名付き URL（R2 へ直接）のアップロードが失敗したら、以後はサーバー経由で送る。 */
    std::atomic<bool> directUploadBroken { false };

    JUCE_DECLARE_NON_COPYABLE (SyncManager)
};
