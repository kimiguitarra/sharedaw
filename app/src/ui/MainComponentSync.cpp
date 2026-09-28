// MainComponent の同期まわり（仕様書 §4）。

#include "MainComponent.h"

#include "Dialogs.h"
#include "ProjectPicker.h"
#include "SyncUI.h"
#include "Theme.h"
#include "sync/CredentialStore.h"
#include "sync/SyncManager.h"

namespace
{
    juce::String joinNames (SyncManager& sync, const std::vector<std::string>& ids)
    {
        juce::StringArray names;

        for (auto& id : ids)
            names.add (sync.scopeName (id));

        return names.joinIntoString ("、"_ju);
    }

    std::unique_ptr<juce::DocumentWindow> makeToolWindow (const juce::String& title, juce::Component* content)
    {
        struct ToolWindow  : public juce::DocumentWindow
        {
            ToolWindow (const juce::String& t) : DocumentWindow (t, Theme::panel, DocumentWindow::closeButton) {}
            void closeButtonPressed() override   { setVisible (false); }
        };

        auto w = std::make_unique<ToolWindow> (title);
        w->setUsingNativeTitleBar (true);
        w->setContentOwned (content, true);
        w->setResizable (true, false);
        w->setAlwaysOnTop (true);
        w->centreWithSize (w->getWidth(), w->getHeight());
        w->setVisible (true);
        return w;
    }
}

bool MainComponent::ensureSyncReady (bool needLinked)
{
    if (! sync.hasCredentials())
    {
        Dialogs::showInfo ("同期"_ju, "先に「同期 → サーバー設定」でサーバー URL とトークンを設定してください。"_ju);
        return false;
    }

    if (needLinked && ! sync.isLinked())
    {
        Dialogs::showInfo ("同期"_ju, "このプロジェクトはまだサーバーに登録されていません。「同期 → サーバーに登録」を使ってください。"_ju);
        return false;
    }

    return true;
}

void MainComponent::showServerSettings()
{
    auto* content = new SyncUI::ServerSettings (sync.getServerUrl(), sync.hasCredentials(),
                                                [this] (const juce::String& url, const juce::String& token)
    {
        if (url.trim().isEmpty())
            return "サーバー URL を入力してください"_ju;

        sync.setCredentials (url, token);

        ApiResponse r;
        SyncUI::runWithProgress ("接続テスト"_ju, [&] { r = sync.fetchMe(); return juce::Result::ok(); });

        if (! r.ok())
            return "接続できませんでした: "_ju + r.message();

        return "接続できました（ユーザー: "_ju + toJuce (r.body.value ("displayName", std::string())) + "）"_ju;
    });

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (content);
    o.dialogTitle = "サーバー設定"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.launchAsync();
}

void MainComponent::registerProject()
{
    if (! ensureSyncReady (false))
        return;

    if (sync.isLinked())
        return Dialogs::showInfo ("同期"_ju, "このプロジェクトは既にサーバーに登録されています。"_ju);

    // まだ保存していない曲は、どこに置くかを聞いて保存してから登録する
    if (! document.hasLocation())
    {
        chooseProjectParent ("曲を置くフォルダを選んでください"_ju, [this] (const juce::File& dir)
        {
            if (auto r = document.saveNew (dir); r.failed())
                return Dialogs::showError ("保存に失敗しました"_ju, r.getErrorMessage());

            settings.setValue ("lastProjectDir", document.getProjectDir().getFullPathName());
            ProjectPicker::remember (settings, document.getProjectDir());
            uploadRegistration();
        });
        return;
    }

    if (document.isDirty())
    {
        if (auto r = document.save(); r.failed())
            return Dialogs::showError ("保存に失敗しました"_ju, r.getErrorMessage());
    }

    uploadRegistration();
}

void MainComponent::chooseProjectParent (const juce::String& title, std::function<void (const juce::File&)> onChosen)
{
    // 前回選んだ場所（なければドキュメント/ShareDAW）から始める。選んだ場所は次の既定にする
    auto start = ProjectPicker::projectsFolder (settings);

    if (! start.isDirectory())
        start.createDirectory();

    auto chooser = std::make_shared<juce::FileChooser> (title, start.isDirectory() ? start
                                                                                  : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser, onChosen = std::move (onChosen)] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();

        if (dir == juce::File())
            return;   // キャンセル

        if (! dir.isDirectory() && ! dir.createDirectory())
            return Dialogs::showError ("フォルダを使えません"_ju, dir.getFullPathName());

        settings.setValue ("projectsFolder", dir.getFullPathName());
        settings.saveIfNeeded();
        onChosen (dir);
    });
}

void MainComponent::uploadRegistration()
{
    const auto snapshot = document.getProject();
    const auto dir = document.getProjectDir();
    auto r = SyncUI::runWithProgress ("サーバーに登録しています"_ju, [&] (const SyncProgress& p) { return sync.runRegister (snapshot, dir, p); });

    if (r.failed())
        return Dialogs::showError ("登録できませんでした"_ju, r.getErrorMessage()
                                     + "\n\n"_ju + "もう一度「サーバーにアップ」を押すと、続きから登録します。"_ju);

    sync.applyRegistered (snapshot, sync.getMeta().baseRevision);
    setStatus ("サーバーに登録しました（リビジョン "_ju + juce::String (sync.getMeta().baseRevision) + "）"_ju);
    toast.show ("サーバーにアップしました"_ju, "仲間は「楽曲を選ぶ」からダウンロードして一緒に作業できます。"_ju, {}, {}, Theme::ok);

    if (syncPanel.isCollapsed())
        toggleSyncPanel();
}

void MainComponent::createProjectOnServer()
{
    if (! sync.hasCredentials())
    {
        Dialogs::showInfo ("サーバーに新しい曲を作る"_ju, "先にサーバー URL とトークンを設定してください。"_ju);
        return showServerSettings();
    }

    confirmDiscardChanges ([this]
    {
        Dialogs::askText ("サーバーに新しい曲を作る"_ju, "曲名"_ju, "新しい曲"_ju, [this] (const juce::String& name)
        {
            bridge.stop();
            document.newProject (name.trim().isEmpty() ? juce::String ("無題"_ju) : name.trim());
            state.selectedTrackId = {};
            state.selectClip ({});
            state.timeline.scrollTick = 0;
            state.changed();
            bridge.returnToStart();

            // 置き場所を聞いてこの PC に保存し、サーバーにも作る
            registerProject();
        });
    });
}

void MainComponent::toggleSyncPanel()
{
    syncPanel.setCollapsed (! syncPanel.isCollapsed());
    resized();
    commandManager.commandStatusChanged();

    if (! syncPanel.isCollapsed())
        sync.checkServerNow();
}

void MainComponent::onIncomingRevisions (const std::vector<SyncManager::RevisionInfo>& revs)
{
    juce::StringArray authors, messages;

    for (auto& r : revs)
    {
        authors.addIfNotAlreadyThere (r.author);

        if (r.message.isNotEmpty())
            messages.add (r.message);
    }

    // 何が変わったか（トラック名など）と、競合があるか
    juce::StringArray scopes;
    int conflicts = 0;

    for (auto& st : sync.scopeStates())
    {
        if (st.theirs)
            scopes.addIfNotAlreadyThere (toJuce (st.name));

        conflicts += st.conflict ? 1 : 0;
    }

    const auto title = authors.joinIntoString ("・"_ju) + " さんがアップしました"_ju;
    auto body = scopes.isEmpty() ? juce::String() : scopes.joinIntoString ("、"_ju) + " が新しくなりました"_ju;

    if (! messages.isEmpty())
        body = "「"_ju + messages[0] + "」 "_ju + body;

    // 自動ダウンロードは、競合がなく、再生・録音していないときだけ
    if (syncPanel.autoPullEnabled() && conflicts == 0 && ! bridge.isPlaying() && ! bridge.isRecording())
    {
        downloadWithChoices ({}, true);
        return;
    }

    if (conflicts > 0)
        body << "（競合 "_ju << conflicts << " 件: 同期パネルで採用する版を選んでください）"_ju;

    toast.show (title, body, conflicts > 0 ? "同期パネル"_ju : "ダウンロード"_ju, [this, conflicts]
    {
        if (conflicts > 0)
        {
            if (syncPanel.isCollapsed())
                toggleSyncPanel();
        }
        else
        {
            downloadWithChoices ({}, false);
        }
    }, conflicts > 0 ? Theme::warning : Theme::accent);
}

bool MainComponent::downloadWithChoices (const std::map<std::string, collab::Resolution>& choices, bool quiet)
{
    if (! ensureSyncReady (true))
        return false;

    if (! quiet)
    {
        auto preview = std::make_shared<SyncManager::PullPreview>();
        const auto dir = document.getProjectDir();
        auto r = SyncUI::runWithProgress ("サーバーを確認しています"_ju, [&] (const SyncProgress& p)
        {
            if (auto res = sync.fetchPullPreview (*preview); res.failed())
                return res;

            if (preview->head == sync.getMeta().baseRevision)
                return juce::Result::ok();

            return sync.runDownloadAudio (preview->headProject, dir, p);
        });

        if (r.failed())
        {
            Dialogs::showError ("ダウンロードできませんでした"_ju, r.getErrorMessage());
            return false;
        }

        if (preview->head == sync.getMeta().baseRevision)
        {
            setStatus ("サーバーと同じ状態です"_ju);
            return true;
        }

        // 競合があって選ばれていなければ、同期パネルで選んでもらう
        if (auto* base = sync.getBase())
            for (auto& st : collab::syncStates (*base, document.getProject(), &preview->headProject))
                if (st.conflict && choices.count (st.id) == 0)
                {
                    if (syncPanel.isCollapsed())
                        toggleSyncPanel();

                    sync.checkServerNow();
                    Dialogs::showInfo ("競合があります"_ju, "「"_ju + toJuce (st.name)
                                         + "」は、あなたとサーバーの両方で変更されています。同期パネルで、採用する版（自分 / サーバー / 両方）を選んでからダウンロードしてください。"_ju);
                    return false;
                }

        bridge.stop();
        sync.applyDownload (*preview, choices);
        syncPanel.clearAfterSync();
        setStatus ("サーバーの新しい変更をダウンロードしました"_ju);
        return true;
    }

    // 自動: 画面を止めずにバックグラウンドで準備してから反映する
    if (autoPullRunning)
        return false;

    autoPullRunning = true;
    auto preview = std::make_shared<SyncManager::PullPreview>();
    const auto dir = document.getProjectDir();
    const int baseAtStart = sync.getMeta().baseRevision;

    juce::Thread::launch ([safe = juce::Component::SafePointer<MainComponent> (this), &syncRef = sync, preview, dir, baseAtStart]
    {
        auto r = syncRef.fetchPullPreview (*preview);

        if (r.wasOk() && preview->head != baseAtStart)
            r = syncRef.runDownloadAudio (preview->headProject, dir);

        juce::MessageManager::callAsync ([safe, preview, r, baseAtStart]
        {
            if (safe == nullptr)
                return;

            auto& self = *safe;
            self.autoPullRunning = false;

            if (r.failed())
                return self.setStatus ("自動ダウンロードに失敗しました: "_ju + r.getErrorMessage());

            if (preview->head == baseAtStart || self.sync.getMeta().baseRevision != baseAtStart)
                return;

            // 準備している間に競合ができていたら、自動では取り込まない
            if (auto* base = self.sync.getBase())
                for (auto& st : collab::syncStates (*base, self.document.getProject(), &preview->headProject))
                    if (st.conflict)
                        return self.toast.show ("競合があります"_ju, "同期パネルで採用する版を選んでください"_ju, "同期パネル"_ju,
                                                [s = safe] { if (s != nullptr && s->syncPanel.isCollapsed()) s->toggleSyncPanel(); },
                                                Theme::warning);

            if (self.bridge.isPlaying() || self.bridge.isRecording())
                return;

            juce::StringArray scopes;
            for (auto& c : preview->diff.changes)
                scopes.addIfNotAlreadyThere (toJuce (c.scopeName));

            self.sync.applyDownload (*preview, {});
            self.setStatus ("他の人の変更を自動でダウンロードしました"_ju);
            self.toast.show ("他の人の変更をダウンロードしました"_ju, scopes.joinIntoString ("、"_ju), {}, {}, Theme::ok);
        });
    });

    return false;
}

void MainComponent::uploadFromPanel (const std::set<std::string>& excluded, const juce::String& message,
                                     const std::map<std::string, collab::Resolution>& choices)
{
    if (! ensureSyncReady (true))
        return;

    // サーバーに新しい版があれば、先に選んだとおりにダウンロードする
    if (sync.headPreview() != nullptr && ! downloadWithChoices (choices, false))
        return;

    std::set<std::string> scopes;

    if (auto* base = sync.getBase())
        for (auto& st : collab::syncStates (*base, document.getProject(), nullptr))
            if (st.mine && excluded.count (st.id) == 0)
                scopes.insert (st.id);

    if (scopes.empty())
        return setStatus ("アップする変更はありません"_ju);

    auto plan = std::make_shared<SyncManager::UploadPlan>();
    const auto snapshot = document.getProject();
    auto r = SyncUI::runWithProgress ("サーバーを確認しています"_ju, [&] { return sync.fetchUploadPlan (snapshot, scopes, *plan); });

    if (r.failed())
        return Dialogs::showError ("アップできませんでした"_ju, r.getErrorMessage());

    if (plan->needsDownload)
    {
        sync.checkServerNow();
        return Dialogs::showInfo ("アップ"_ju, "たった今、他の人がアップしました。同期パネルで確認してから、もう一度アップしてください。"_ju);
    }

    if (! plan->staleRenders.empty())
    {
        auto text = "外部プラグインを使うトラックはバウンスしてからアップしてください: "_ju + joinNames (sync, plan->staleRenders);

        for (auto& id : plan->staleRenders)
            if (bridge.isPlayingRender (id))
            {
                text << "\n\n"
                     << "この環境で鳴らせないプラグインのトラックは、ここではバウンスできません。"_ju
                     << "ノートなど音の元の変更を元に戻すか、プラグインの持ち主にバウンスしてもらってください。"_ju;
                break;
            }

        return Dialogs::showError ("バウンスが必要です"_ju, text);
    }

    if (document.hasLocation())
        document.save();

    int revision = 0;
    const auto dir = document.getProjectDir();
    auto res = SyncUI::runWithProgress ("アップしています"_ju,
                                        [&] (const SyncProgress& p) { return sync.runUpload (*plan, message, dir, revision, p); });

    if (res.failed())
    {
        sync.checkServerNow();
        return Dialogs::showError ("アップできませんでした"_ju, res.getErrorMessage());
    }

    sync.applyUploaded (*plan, revision);
    syncPanel.clearAfterSync();
    setStatus ("アップしました（"_ju + joinNames (sync, std::vector<std::string> (scopes.begin(), scopes.end())) + "）"_ju);
}

void MainComponent::showProjectPicker()
{
    ProjectPicker::Callbacks cb;

    cb.openLocal = [this] (const juce::File& folder, bool pullAfter)
    {
        confirmDiscardChanges ([this, folder, pullAfter]
        {
            openProjectFolder (folder);

            // サーバーに新しい版があれば、そのまま取り込みの画面を出す
            if (pullAfter && sync.isLinked())
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                {
                    if (safe != nullptr)
                        safe->downloadWithChoices ({}, false);
                });
        });
    };

    cb.download = [this] (const std::string& projectId)
    {
        confirmDiscardChanges ([this, projectId] { downloadProject (projectId); });
    };

    cb.createOnServer = [this] { createProjectOnServer(); };
    cb.serverSettings = [this] { showServerSettings(); };

    juce::DialogWindow::LaunchOptions o;
    // 「いま開いている」は実際に開いている曲だけ（起動直後はまだ何も開いていない）
    o.content.setOwned (new ProjectPicker (sync, settings, document.hasLocation() ? document.getProjectDir() : juce::File(),
                                           std::move (cb)));
    o.dialogTitle = "楽曲を選ぶ"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::downloadProject (const std::string& projectId)
{
    if (! ensureSyncReady (false))
        return;

    // どこに置くかを聞いて、その中に曲のフォルダを作る
    chooseProjectParent ("ダウンロードした曲を置くフォルダを選んでください"_ju, [this, projectId] (const juce::File& dir)
    {
        juce::File created;
        auto res = SyncUI::runWithProgress ("ダウンロードしています"_ju, [&] (const SyncProgress& p) { return sync.runOpenFromServer (projectId, dir, created, p); });

        if (res.failed())
            return Dialogs::showError ("開けませんでした"_ju, res.getErrorMessage());

        openProjectFolder (created);
    });
}

void MainComponent::showHistory()
{
    if (! ensureSyncReady (true))
        return;

    nlohmann::json list;
    auto r = SyncUI::runWithProgress ("履歴を取得しています"_ju, [&] { return sync.fetchRevisions (list); });

    if (r.failed())
        return Dialogs::showError ("取得できませんでした"_ju, r.getErrorMessage());

    diffWindow = makeToolWindow ("リビジョン履歴"_ju, SyncUI::createHistoryView (list).release());
}

void MainComponent::jumpTo (const collab::Change& c)
{
    if (c.scopeKind == collab::ScopeKind::track && document.getProject().findTrack (c.scopeId) != nullptr)
    {
        state.selectedTrackId = c.scopeId;
        state.selectClip ({});
    }

    if (c.fromTick >= 0)
    {
        state.timeline.scrollTick = juce::jmax (0.0, (double) c.fromTick - collab::kPpq * 2);
        bridge.setPositionTick ((double) c.fromTick);
    }

    state.changed();
}
