// MainComponent の同期まわり（仕様書 §4）。

#include "MainComponent.h"

#include "Dialogs.h"
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

    auto doRegister = [this]
    {
        const auto snapshot = document.getProject();
        const auto dir = document.getProjectDir();
        auto r = SyncUI::runWithProgress ("サーバーに登録しています"_ju, [&] { return sync.runRegister (snapshot, dir); });

        if (r.failed())
            return Dialogs::showError ("登録できませんでした"_ju, r.getErrorMessage());

        sync.applyRegistered (snapshot, sync.getMeta().baseRevision);
        SyncUI::runWithProgress ("ロックを確認しています"_ju, [this] { return sync.fetchLocks(); });
        setStatus ("サーバーに登録しました（リビジョン "_ju + juce::String (sync.getMeta().baseRevision) + "）"_ju);
    };

    // 保存されていないと .collab を置けない
    if (! document.hasLocation() || document.isDirty())
        saveProject ([doRegister] (bool ok) { if (ok) doRegister(); });
    else
        doRegister();
}

void MainComponent::openFromServer()
{
    if (! ensureSyncReady (false))
        return;

    nlohmann::json list;
    auto r = SyncUI::runWithProgress ("プロジェクト一覧を取得しています"_ju, [&] { return sync.fetchProjects (list); });

    if (r.failed())
        return Dialogs::showError ("取得できませんでした"_ju, r.getErrorMessage());

    if (list.empty())
        return Dialogs::showInfo ("サーバーから開く"_ju, "参加しているプロジェクトがありません。"_ju);

    juce::PopupMenu menu;
    int id = 1;

    for (auto& p : list)
    {
        const auto pid = p.value ("id", std::string());
        menu.addItem (id++, toJuce (p.value ("name", std::string())) + "  (rev " + juce::String (p.value ("headRevision", 0)) + ")");
        juce::ignoreUnused (pid);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&transport), [this, list] (int chosen)
    {
        if (chosen <= 0)
            return;

        const auto projectId = list[(size_t) chosen - 1].value ("id", std::string());

        confirmDiscardChanges ([this, projectId]
        {
            chooser = std::make_unique<juce::FileChooser> ("保存先のフォルダを選択（中にプロジェクトのフォルダを作ります）"_ju,
                                                           juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this, projectId] (const juce::FileChooser& fc)
            {
                auto dir = fc.getResult();

                if (dir == juce::File())
                    return;

                juce::File created;
                auto res = SyncUI::runWithProgress ("ダウンロードしています"_ju,
                                                    [&] { return sync.runOpenFromServer (projectId, dir, created); });

                if (res.failed())
                    return Dialogs::showError ("開けませんでした"_ju, res.getErrorMessage());

                openProjectFolder (created);
                SyncUI::runWithProgress ("ロックを確認しています"_ju, [this] { return sync.fetchLocks(); });
            });
        });
    });
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
                                            [&] { return sync.runDownloadAudio (preview->headProject, document.getProjectDir()); });

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

    if (! plan->notLocked.empty())
        return Dialogs::showError ("ロックが必要です"_ju, "次のトラックのロックを持っていません: "_ju + joinNames (sync, plan->notLocked));

    if (! plan->staleRenders.empty())
    {
        auto message = "外部プラグインを使うトラックはバウンスしてから push してください: "_ju + joinNames (sync, plan->staleRenders);

        for (auto& id : plan->staleRenders)
            if (bridge.isPlayingRender (id))
            {
                message << "\n\n"
                        << "この環境で鳴らせないプラグインのトラックは、ここではバウンスできません。"_ju
                        << "ノートなど音の元の変更を元に戻すか、プラグインの持ち主にバウンスしてもらってください。"_ju;
                break;
            }

        return Dialogs::showError ("バウンスが必要です"_ju, message);
    }

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
                                            [&] { return sync.runPush (*plan, message, release, dir, revision); });

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
        state.selectedClipId = {};
    }

    if (c.fromTick >= 0)
    {
        state.timeline.scrollTick = juce::jmax (0.0, (double) c.fromTick - collab::kPpq * 2);
        bridge.setPositionTick ((double) c.fromTick);
    }

    state.changed();
}
