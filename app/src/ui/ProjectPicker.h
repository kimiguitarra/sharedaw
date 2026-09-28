#pragma once

#include "Common.h"
#include "sync/SyncManager.h"

/**
    「楽曲」画面（起動時・ファイル → 楽曲を選ぶ）。Google Drive のように、サーバーにある曲の一覧から開く。

    曲はサーバーで作り、ダウンロードして編集する（この PC だけにある曲という考え方はない）。
    曲を右クリック（または Delete / F2）で、開く・名前を変更・削除。
*/
class ProjectPicker  : public juce::Component,
                       private juce::ListBoxModel
{
public:
    enum class Status
    {
        upToDate,        // 最新
        localChanges,    // この PC に、まだアップしていない変更
        serverNewer,     // サーバーに新しい変更
        both,            // 両方に変更
        serverOnly,      // まだダウンロードしていない
        offline          // サーバーを確認できない（この PC のコピーを開ける）
    };

    struct Entry
    {
        Status status = Status::serverOnly;
        juce::String name;
        std::string projectId;
        std::optional<SyncManager::LocalInfo> local;   // ダウンロード済みなら、この PC のフォルダ
        int headRevision = 0;
        juce::Time updatedAt;
        juce::String updatedBy;
    };

    struct Callbacks
    {
        std::function<void (const juce::File& folder, bool downloadAfterOpen)> openLocal;
        std::function<void (const std::string& projectId)> download;
        std::function<void()> createOnServer, serverSettings;
    };

    ProjectPicker (SyncManager&, juce::PropertiesFile& settings, juce::File currentFolder, Callbacks);
    ~ProjectPicker() override;

    /** 一覧を作り直す（サーバーはバックグラウンドで取得する）。 */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    //==============================================================================
    // この PC にある曲のフォルダ（開いた・ダウンロードしたもの）
    static void remember (juce::PropertiesFile&, const juce::File& projectFolder);
    static juce::Array<juce::File> knownFolders (juce::PropertiesFile&);

    /** サーバーからダウンロードした曲を置くフォルダ（既定: ドキュメント/ShareDAW）。 */
    static juce::File projectsFolder (juce::PropertiesFile&);

    static juce::String statusText (const Entry&);
    static juce::Colour statusColour (Status);

private:
    SyncManager& sync;
    juce::PropertiesFile& settings;
    juce::File currentFolder;
    Callbacks callbacks;

    juce::Label serverLine, folderLine;
    juce::ListBox list { {}, this };
    juce::TextButton createButton { "＋ 新しい曲"_ju }, refreshButton { "更新"_ju }, serverButton { "サーバー設定…"_ju };
    juce::TextButton folderButton { "変更…"_ju }, openButton { "開く"_ju }, closeButton { "閉じる"_ju };

    std::vector<SyncManager::LocalInfo> locals;
    std::optional<nlohmann::json> serverList;
    juce::String serverError;
    bool loadingServer = false;
    std::vector<Entry> entries;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    void rebuildEntries();
    void updateButtons();
    const Entry* selected() const;
    void openSelected();
    void renameSelected();
    void deleteSelected();
    void showMenu (int row);
    void close();

    int getNumRows() override                            { return (int) entries.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int) override              { updateButtons(); }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override   { openSelected(); }
    void returnKeyPressed (int) override                 { openSelected(); }
    void deleteKeyPressed (int) override                 { deleteSelected(); }
};
