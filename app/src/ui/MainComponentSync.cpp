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

    // まだ保存していない曲は、ダウンロード先と同じフォルダ（ドキュメント/ShareDAW）に保存してから登録する
    if (! document.hasLocation())
    {
        auto dir = ProjectPicker::projectsFolder (settings);

        if (! dir.isDirectory() && ! dir.createDirectory())
            return Dialogs::showError ("登録できませんでした"_ju, "フォルダを作れません: "_ju + dir.getFullPathName());

        if (auto r = document.saveNew (dir); r.failed())
            return Dialogs::showError ("保存に失敗しました"_ju, r.getErrorMessage());

        settings.setValue ("lastProjectDir", document.getProjectDir().getFullPathName());
        ProjectPicker::remember (settings, document.getProjectDir());
    }
    else if (document.isDirty())
    {
        if (auto r = document.save(); r.failed())
            return Dialogs::showError ("保存に失敗しました"_ju, r.getErrorMessage());
    }

    const auto snapshot = document.getProject();
    const auto dir = document.getProjectDir();
    auto r = SyncUI::runWithProgress ("サーバーに登録しています"_ju, [&] (const SyncProgress& p) { return sync.runRegister (snapshot, dir, p); });

    if (r.failed())
        return Dialogs::showError ("登録できませんでした"_ju, r.getErrorMessage()
                                     + "\n\n"_ju + "もう一度「サーバーにアップ」を押すと、続きから登録します。"_ju);

    sync.applyRegistered (snapshot, sync.getMeta().baseRevision);
    SyncUI::runWithProgress ("ロックを確認しています"_ju, [this] { return sync.fetchLocks(); });
    setStatus ("サーバーに登録しました（リビジョン "_ju + juce::String (sync.getMeta().baseRevision) + "）"_ju);
    toast.show ("サーバーにアップしました"_ju, "仲間は「楽曲を選ぶ」からダウンロードして一緒に作業できます。"_ju, {}, {}, Theme::green);

    if (! syncPanel.isVisible())
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

            // サーバーに作って、この PC（ドキュメント/ShareDAW）にも置く
            registerProject();
        });
    });
}

void MainComponent::toggleSyncPanel()
{
    syncPanel.setVisible (! syncPanel.isVisible());
    settings.setValue ("syncPanelVisible", syncPanel.isVisible());
    resized();
    commandManager.commandStatusChanged();

    if (syncPanel.isVisible())
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

    // 何が変わったか（トラック名など）
    juce::StringArray scopes;

    if (auto st = sync.getServerStatus(); st.preview != nullptr)
        for (auto& c : st.preview->diff.changes)
            scopes.addIfNotAlreadyThere (toJuce (c.scopeName));

    const auto title = authors.joinIntoString ("・"_ju) + " さんがアップしました"_ju;
    auto body = scopes.isEmpty() ? juce::String() : scopes.joinIntoString ("、"_ju) + " が変わりました"_ju;

    if (! messages.isEmpty())
        body = "「"_ju + messages[0] + "」 "_ju + body;

    if (syncPanel.autoPullEnabled())
        return pullNow (true);

    toast.show (title, body, "取り込む"_ju, [this] { pullNow(); }, Theme::accent);
}

void MainComponent::pullNow (bool quiet)
{
    if (! ensureSyncReady (true))
        return;

    if (! quiet)
    {
        auto preview = std::make_shared<SyncManager::PullPreview>();
        auto r = SyncUI::runWithProgress ("サーバーを確認しています"_ju, [&] { return sync.fetchPullPreview (*preview); });

        if (r.failed())
            return Dialogs::showError ("取り込めませんでした"_ju, r.getErrorMessage());

        if (preview->head == sync.getMeta().baseRevision)
            return setStatus ("最新です（リビジョン "_ju + juce::String (preview->head) + "）"_ju);

        return applyPullPreview (*preview);
    }

    // 自動の取り込み: 画面を止めずにバックグラウンドで準備し、再生・録音中でなければ反映する
    if (autoPullRunning)
        return;

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
                return self.setStatus ("自動の取り込みに失敗しました: "_ju + r.getErrorMessage());

            if (preview->head == baseAtStart || self.sync.getMeta().baseRevision != baseAtStart)
                return;

            if (self.bridge.isPlaying() || self.bridge.isRecording())
                return self.toast.show ("新しい変更があります"_ju, "再生中なので、止めてから取り込んでください。"_ju, "取り込む"_ju,
                                        [s = safe] { if (s != nullptr) s->pullNow(); }, Theme::accent);

            auto report = self.sync.applyPull (*preview);
            juce::StringArray scopes;

            for (auto& c : preview->diff.changes)
                scopes.addIfNotAlreadyThere (toJuce (c.scopeName));

            self.setStatus ("リビジョン "_ju + juce::String (preview->head) + " を自動で取り込みました"_ju);
            self.toast.show ("他の人の変更を取り込みました"_ju,
                             scopes.joinIntoString ("、"_ju) + (report.conflicts.empty() ? juce::String() : "（不整合あり: 取り込む前の状態を保存しました）"_ju),
                             {}, {}, report.conflicts.empty() ? Theme::green : Theme::warning);
        });
    });
}

bool MainComponent::pushBlocked (const SyncManager::PushPlan& plan)
{
    if (! plan.notLocked.empty())
    {
        Dialogs::showError ("ロックが必要です"_ju, "次のトラックのロックを持っていません: "_ju + joinNames (sync, plan.notLocked));
        return true;
    }

    if (! plan.staleRenders.empty())
    {
        auto message = "外部プラグインを使うトラックはバウンスしてからアップしてください: "_ju + joinNames (sync, plan.staleRenders);

        for (auto& id : plan.staleRenders)
            if (bridge.isPlayingRender (id))
            {
                message << "\n\n"
                        << "この環境で鳴らせないプラグインのトラックは、ここではバウンスできません。"_ju
                        << "ノートなど音の元の変更を元に戻すか、プラグインの持ち主にバウンスしてもらってください。"_ju;
                break;
            }

        Dialogs::showError ("バウンスが必要です"_ju, message);
        return true;
    }

    return false;
}

void MainComponent::pushFromPanel (const juce::String& message, bool release)
{
    if (! ensureSyncReady (true))
        return;

    auto plan = std::make_shared<SyncManager::PushPlan>();
    const auto snapshot = document.getProject();
    auto r = SyncUI::runWithProgress ("サーバーを確認しています"_ju, [&] { return sync.fetchPushPlan (snapshot, *plan); });

    if (r.failed())
        return Dialogs::showError ("アップロードできませんでした"_ju, r.getErrorMessage());

    if (plan->diff.empty())
        return setStatus ("アップロードする変更はありません"_ju);

    if (plan->needsPull)
    {
        // 先に取り込んでから、続けてアップする（自分の変更はロックしているので残る）
        return Dialogs::confirm ("先に取り込みます"_ju,
                                 "サーバーに新しいリビジョン（"_ju + juce::String (plan->head) + "）があります。取り込んでからアップしますか？"_ju,
                                 "取り込んでアップ"_ju, [this, message, release, head = plan->head]
        {
            pullNow();

            // 取り込めたら（ベースがサーバーのヘッドに追いついたら）続けてアップする
            if (sync.getMeta().baseRevision >= head)
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), message, release]
                {
                    if (safe != nullptr)
                        safe->pushFromPanel (message, release);
                });
        });
    }

    if (pushBlocked (*plan))
        return;

    runPushPlan (*plan, message, release);
    syncPanel.clearComment();
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
                        safe->pull();
                });
        });
    };

    cb.download = [this] (const std::string& projectId)
    {
        confirmDiscardChanges ([this, projectId] { downloadProject (projectId); });
    };

    cb.newProject = [this] { newProject(); };
    cb.createOnServer = [this] { createProjectOnServer(); };
    cb.openAndUpload = [this] (const juce::File& folder)
    {
        confirmDiscardChanges ([this, folder]
        {
            openProjectFolder (folder);
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
            {
                if (safe != nullptr)
                    safe->registerProject();
            });
        });
    };
    cb.openOther = [this] { openProject(); };
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

    // ダウンロード先（ドキュメント/ShareDAW など）の中に曲のフォルダを作る
    auto dir = ProjectPicker::projectsFolder (settings);

    if (! dir.isDirectory() && ! dir.createDirectory())
        return Dialogs::showError ("開けませんでした"_ju, "ダウンロード先のフォルダを作れません: "_ju + dir.getFullPathName());

    juce::File created;
    auto res = SyncUI::runWithProgress ("ダウンロードしています"_ju, [&] (const SyncProgress& p) { return sync.runOpenFromServer (projectId, dir, created, p); });

    if (res.failed())
        return Dialogs::showError ("開けませんでした"_ju, res.getErrorMessage());

    openProjectFolder (created);
    SyncUI::runWithProgress ("ロックを確認しています"_ju, [this] { return sync.fetchLocks(); });
}

void MainComponent::pull()
{
    if (! ensureSyncReady (true))
        return;

    auto preview = std::make_shared<SyncManager::PullPreview>();
    auto r = SyncUI::runWithProgress ("サーバーを確認しています"_ju, [&] { return sync.fetchPullPreview (*preview); });

    if (r.failed())
        return Dialogs::showError ("取り込めませんでした"_ju, r.getErrorMessage());

    if (preview->head == sync.getMeta().baseRevision)
        return Dialogs::showInfo ("取り込み"_ju, "最新です（リビジョン "_ju + juce::String (preview->head) + "）"_ju);

    const auto headline = "リビジョン "_ju + juce::String (sync.getMeta().baseRevision) + " → "_ju + juce::String (preview->head)
                            + " の変更（ベース → ヘッド）"_ju;

    auto* view = new SyncUI::DiffView (SyncUI::DiffView::Mode::pull, preview->diff, headline,
                                       [this] (const collab::Change& c) { jumpTo (c); },
                                       [this, preview] (const juce::String&, bool)
    {
        // ボタンの処理中に自分のウィンドウを消さないよう、次のメッセージで実行する
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), preview]
        {
            if (safe != nullptr)
                safe->applyPullPreview (*preview);
        });
    },
                                       [this] { closeDiffWindowAsync(); });

    diffWindow = makeToolWindow ("取り込み（pull）"_ju, view);
}

void MainComponent::applyPullPreview (const SyncManager::PullPreview& previewRef)
{
    {
        diffWindow = nullptr;
        auto preview = &previewRef;

        auto res = SyncUI::runWithProgress ("オーディオをダウンロードしています"_ju,
                                            [&] (const SyncProgress& p) { return sync.runDownloadAudio (preview->headProject, document.getProjectDir(), p); });

        if (res.failed())
            return Dialogs::showError ("取り込めませんでした"_ju, res.getErrorMessage());

        bridge.stop();
        auto report = sync.applyPull (*preview);

        juce::String message = "リビジョン "_ju + juce::String (preview->head) + " を取り込みました。"_ju;

        if (! report.keptLocal.empty())
            message << "\n" << "ローカルを維持: "_ju << joinNames (sync, report.keptLocal);

        if (! report.conflicts.empty())
        {
            message << "\n\n" << "不整合を検知しました: "_ju << joinNames (sync, report.conflicts)
                    << "\n" << "取り込み前のローカルを保存しました: "_ju << report.conflictBackup.getFullPathName();
            Dialogs::showError ("取り込み（不整合あり）"_ju, message);
        }
        else
        {
            setStatus (message.replace ("\n", " "));
        }
    }
}

void MainComponent::push()
{
    if (! ensureSyncReady (true))
        return;

    auto plan = std::make_shared<SyncManager::PushPlan>();
    const auto snapshot = document.getProject();
    auto r = SyncUI::runWithProgress ("サーバーを確認しています"_ju, [&] { return sync.fetchPushPlan (snapshot, *plan); });

    if (r.failed())
        return Dialogs::showError ("アップロードできませんでした"_ju, r.getErrorMessage());

    if (plan->needsPull)
        return Dialogs::showInfo ("アップロード"_ju, "サーバーに新しいリビジョン（"_ju + juce::String (plan->head)
                                                     + "）があります。先に取り込んでください。"_ju);

    if (plan->diff.empty())
        return Dialogs::showInfo ("アップロード"_ju, "アップロードする変更はありません。"_ju);

    if (pushBlocked (*plan))
        return;

    const auto headline = "リビジョン "_ju + juce::String (sync.getMeta().baseRevision) + " からの変更（ベース → ローカル）"_ju;

    auto* view = new SyncUI::DiffView (SyncUI::DiffView::Mode::push, plan->diff, headline,
                                       [this] (const collab::Change& c) { jumpTo (c); },
                                       [this, plan] (const juce::String& message, bool release)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), plan, message, release]
        {
            if (safe != nullptr)
                safe->runPushPlan (*plan, message, release);
        });
    },
                                       [this] { closeDiffWindowAsync(); });

    diffWindow = makeToolWindow ("アップロード（push）"_ju, view);
}

void MainComponent::runPushPlan (const SyncManager::PushPlan& planRef, const juce::String& message, bool release)
{
    {
        diffWindow = nullptr;
        auto plan = &planRef;

        // ローカルにも保存しておく
        if (document.hasLocation())
            document.save();

        int revision = 0;
        const auto dir = document.getProjectDir();
        auto res = SyncUI::runWithProgress ("アップロードしています"_ju,
                                            [&] (const SyncProgress& p) { return sync.runPush (*plan, message, release, dir, revision, p); });

        if (res.failed())
            return Dialogs::showError ("アップロードできませんでした"_ju, res.getErrorMessage());

        sync.applyPushed (*plan, revision);
        setStatus ("リビジョン "_ju + juce::String (revision) + " としてアップロードしました"_ju);
    }
}

void MainComponent::closeDiffWindowAsync()
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
            safe->diffWindow = nullptr;
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

void MainComponent::requestLocks (std::vector<std::string> scopeIds)
{
    juce::StringArray lines;

    for (auto& id : scopeIds)
    {
        auto line = sync.scopeName (id);

        if (auto lock = sync.getLock (id))
            line << "（"_ju << lock->displayName << " がロック中）"_ju;

        lines.add (line);
    }

    Dialogs::confirm ("ロックを取得しますか？"_ju,
                      "このトラックを編集するにはロックが必要です:\n"_ju + lines.joinIntoString ("\n")
                        + "\n\n"_ju + "取得したら、もう一度操作してください。"_ju,
                      "ロックを取得"_ju, [this, scopeIds]
    {
        for (auto& id : scopeIds)
        {
            auto r = SyncUI::runWithProgress ("ロックを取得しています"_ju, [this, scopeId = id] { return sync.runAcquireLock (scopeId); });

            if (r.failed())
                return Dialogs::showError ("ロックを取得できませんでした"_ju, sync.scopeName (id) + ": " + r.getErrorMessage());
        }

        setStatus ("ロックを取得しました: "_ju + joinNames (sync, scopeIds));
    });
}

void MainComponent::lockMenuForScope (const std::string& scopeId, juce::PopupMenu& menu)
{
    if (! sync.isLinked())
        return;

    const auto lock = sync.getLock (scopeId);
    const bool mine = sync.isLockedByMe (scopeId);

    menu.addItem ("ロックを取得"_ju, ! lock.has_value(), false, [this, scopeId]
    {
        auto r = SyncUI::runWithProgress ("ロックを取得しています"_ju, [&] { return sync.runAcquireLock (scopeId); });
        if (r.failed()) Dialogs::showError ("ロックを取得できませんでした"_ju, r.getErrorMessage());
    });

    menu.addItem ("ロックを解除"_ju, mine, false, [this, scopeId]
    {
        if (sync.hasLocalChanges (scopeId))
            Dialogs::showInfo ("ロックの解除"_ju, "未 push の変更があります。解除すると、他の人の変更を取り込んだときに上書きされます。"_ju);

        auto r = SyncUI::runWithProgress ("ロックを解除しています"_ju, [&] { return sync.runReleaseLock (scopeId, false); });
        if (r.failed()) Dialogs::showError ("ロックを解除できませんでした"_ju, r.getErrorMessage());
    });

    if (lock && ! mine)
    {
        menu.addItem ("ロックを強制解除（"_ju + lock->displayName + "）…"_ju, [this, scopeId, name = lock->displayName]
        {
            Dialogs::confirm ("ロックの強制解除"_ju,
                              name + " のロックを強制的に解除します。相手が解除し忘れた場合だけ使ってください（履歴に記録されます）。"_ju,
                              "強制解除"_ju, [this, scopeId]
            {
                auto r = SyncUI::runWithProgress ("ロックを解除しています"_ju, [&] { return sync.runReleaseLock (scopeId, true); });
                if (r.failed()) Dialogs::showError ("解除できませんでした"_ju, r.getErrorMessage());
            });
        });
    }
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
