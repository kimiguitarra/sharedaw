#pragma once

#include "Common.h"
#include "sync/SyncManager.h"

/**
    「楽曲を選ぶ」画面（起動時・ファイル → 楽曲を選ぶ）。
    サーバーの曲とこの PC の曲を 1 つの一覧にまとめ、それぞれの状況（最新・未 push・サーバーに新しい版など）を見せる。
    曲は基本的にサーバーで管理し、この PC にはダウンロードしたフォルダを置く。
*/
class ProjectPicker  : public juce::Component,
                       private juce::ListBoxModel
{
public:
    enum class Status
    {
        upToDate,        // 最新
        localChanges,    // この PC に未送信の変更
        serverNewer,     // サーバーに新しい版
        both,            // 両方に変更
        serverOnly,      // サーバーだけ（ダウンロードして開く）
        localOnly,       // この PC だけ（サーバー未登録）
        unchecked,       // サーバーを確認できない（未設定・オフライン）
        otherServer,     // 別のサーバーの曲
        notOnServer      // サーバーに見当たらない（削除された・参加していない）
    };

    struct Entry
    {
        Status status = Status::localOnly;
        juce::String name;
        std::string projectId;
        std::optional<SyncManager::LocalInfo> local;
        bool onServer = false;
        int headRevision = 0;
        juce::Time updatedAt;
        juce::String updatedBy;
    };

    struct Callbacks
    {
        std::function<void (const juce::File& folder, bool pullAfterOpen)> openLocal;
        std::function<void (const std::string& projectId)> download;
        std::function<void()> newProject, openOther, serverSettings, createOnServer;
        std::function<void (const juce::File& folder)> openAndUpload;   // この PC だけの曲を開いてサーバーにアップする
    };

    ProjectPicker (SyncManager&, juce::PropertiesFile& settings, juce::File currentFolder, Callbacks);
    ~ProjectPicker() override;

    /** 一覧を作り直す（サーバーはバックグラウンドで取得する）。 */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    //==============================================================================
    // この PC にある曲のフォルダ（開いた・保存した・ダウンロードしたもの）
    static void remember (juce::PropertiesFile&, const juce::File& projectFolder);
    static void forget (juce::PropertiesFile&, const juce::File& projectFolder);
    static juce::Array<juce::File> knownFolders (juce::PropertiesFile&);

    /** サーバーからダウンロードした曲を置くフォルダ（既定: ドキュメント/ShareDAW）。 */
    static juce::File projectsFolder (juce::PropertiesFile&);

    static juce::String statusText (const Entry&);
    static juce::String statusShort (const Entry&);
    static juce::Colour statusColour (Status);

private:
    SyncManager& sync;
    juce::PropertiesFile& settings;
    juce::File currentFolder;
    Callbacks callbacks;

    juce::Label serverLine, folderLine;
    juce::ListBox table { {}, this };
    juce::TextButton refreshButton { "更新"_ju }, serverButton { "サーバー設定…"_ju }, folderButton { "変更…"_ju };
    juce::TextButton createButton { "＋ サーバーに新しい曲を作る"_ju };
    juce::TextButton newButton { "この PC だけで新規作成"_ju }, otherButton { "フォルダから開く…"_ju }, forgetButton { "一覧から外す"_ju };
    juce::TextButton openButton { "開く"_ju }, closeButton { "閉じる"_ju };

    std::vector<SyncManager::LocalInfo> locals;
    std::optional<nlohmann::json> serverList;
    juce::String serverError;
    bool loadingServer = false;
    std::vector<Entry> entries;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    void rebuildEntries();
    void updateButtons();
    void openSelected();
    void close();

    int getNumRows() override                            { return (int) entries.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int) override              { updateButtons(); }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override   { openSelected(); }
    void returnKeyPressed (int) override                 { openSelected(); }
};
