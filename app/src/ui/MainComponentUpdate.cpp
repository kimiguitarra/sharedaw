// MainComponent のアプリの更新（ヘルプ → アップデートを確認、起動時の確認）。

#include "MainComponent.h"

#include "Dialogs.h"
#include "sync/SyncManager.h"

void MainComponent::checkForUpdates (bool interactive, std::function<void()> then)
{
    auto proceed = [then] { if (then) then(); };

    // 起動時の自動確認は、CI でビルドしたアプリ（ビルド番号あり）で、同期サーバーを設定済みのときだけ
    if (! interactive && Updater::currentBuild() <= 0)
        return proceed();

    if (! sync.hasCredentials())
    {
        if (interactive)
            Dialogs::showInfo ("アップデート"_ju,
                               "アップデートは同期サーバーから配信されます。\n"_ju
                               "同期 → サーバー設定… でサーバーの URL とトークンを設定してから、もう一度お試しください。"_ju);
        return proceed();
    }

    if (updateCheckRunning.exchange (true))
        return proceed();

    if (then)
        updateBusy = std::make_unique<SyncUI::BusyOverlay> (this, "新しいバージョンを確認しています…"_ju);

    auto client = sync.makeClient();
    juce::Component::SafePointer<MainComponent> safe (this);

    juce::Thread::launch ([client, safe, interactive, then, proceed]
    {
        std::optional<Updater::Info> info;
        auto result = Updater::fetchLatest (client, info);

        juce::MessageManager::callAsync ([safe, interactive, result, info, then, proceed]
        {
            if (safe == nullptr)
                return;

            safe->updateCheckRunning = false;
            safe->updateBusy = nullptr;

            if (result.failed())
            {
                if (interactive)
                    Dialogs::showError ("アップデート"_ju, "更新を確認できませんでした: "_ju + result.getErrorMessage());
                return proceed();
            }

            const int current = Updater::currentBuild();

            if (! info || info->build <= current)
            {
                if (interactive)
                    Dialogs::showInfo ("アップデート"_ju,
                                       ! info ? "配信されている更新はまだありません。"_ju
                                              : "最新のバージョンです（"_ju + Updater::versionText (current) + "）。"_ju);
                return proceed();
            }

            // 自動の確認では「このバージョンをスキップ」したものは聞かない
            if (! interactive && safe->settings.getIntValue ("skippedUpdateBuild") == info->build)
                return proceed();

            safe->offerUpdate (*info, interactive, then);
        });
    });
}

void MainComponent::offerUpdate (const Updater::Info& info, bool interactive, std::function<void()> then)
{
    const int current = Updater::currentBuild();
    auto message = "新しいバージョンがあります。\n\n"_ju
                   + "今: "_ju + Updater::versionText (current) + "\n"
                   + "新: "_ju + Updater::versionText (info.build) + "\n";

    if (info.notes.isNotEmpty())
        message << "\n" << info.notes << "\n";

    message << "\n"_ju << "変わったファイルだけをダウンロードして、このアプリを置き換えます。"_ju;

    // 起動時の確認では「このバージョンをスキップ」も選べる。Enter・スペースでは押されない（押し間違いで更新が始まらないように）
    juce::StringArray buttons { "更新する"_ju };

    if (! interactive)
        buttons.add ("このバージョンをスキップ"_ju);

    buttons.add ("あとで"_ju);

    juce::Component::SafePointer<MainComponent> safe (this);

    Dialogs::askChoice ("アップデート"_ju, message, buttons, [safe, info, interactive, then] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 1)
            return safe->installUpdate (info, then);

        if (result == 2 && ! interactive)
            safe->settings.setValue ("skippedUpdateBuild", info.build);

        if (then)
            then();
    }, this);
}

void MainComponent::installUpdate (const Updater::Info& info, std::function<void()> then)
{
    struct Task  : public juce::ThreadWithProgressWindow
    {
        Task (SyncClient c, Updater::Info i)
            : ThreadWithProgressWindow ("アップデート"_ju, true, true, 10000, "中止"_ju), client (std::move (c)), info (std::move (i)) {}

        void run() override
        {
            result = Updater::downloadAndInstall (client, info, [this] (double p, const juce::String& text)
            {
                setProgress (p);
                setStatusMessage (text);
                return ! threadShouldExit();
            });
        }

        SyncClient client;
        Updater::Info info;
        juce::Result result = juce::Result::ok();
    };

    bridge.stop();
    Task task (sync.makeClient(), info);

    if (! task.runThread() && task.result.wasOk())
        task.result = juce::Result::fail ("中止しました"_ju);

    if (task.result.failed())
    {
        Dialogs::showError ("アップデート"_ju, "更新できませんでした。\n"_ju + task.result.getErrorMessage()
                                                 + "\n\n今のバージョンはそのまま使えます。"_ju);

        if (then)
            then();

        return;
    }

    settings.removeValue ("skippedUpdateBuild");

    // 再起動はいつも聞いてから（勝手に再起動しない）。あとでにしたら、次に起動したときから新しいバージョン
    Dialogs::askChoice ("アップデート"_ju,
                        Updater::versionText (info.build) + " に更新しました。\n再起動すると新しいバージョンになります。"_ju,
                        { "今すぐ再起動"_ju, "あとで"_ju },
                        [safe = juce::Component::SafePointer<MainComponent> (this), then] (int result)
                        {
                            if (result == 1 && safe != nullptr)
                            {
                                // 保存の確認で「キャンセル」したら再起動しない（前は再起動の印だけ残り、あとで普通に終了したときに再起動していた）
                                safe->confirmDiscardChanges ([]
                                {
                                    Updater::requestRelaunch();
                                    juce::JUCEApplication::getInstance()->quit();
                                });
                            }
                            else if (then)
                            {
                                then();
                            }
                        },
                        this);
}

void MainComponent::showStartup()
{
    checkForUpdates (false, [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
            safe->showProjectPicker();
    });
}
