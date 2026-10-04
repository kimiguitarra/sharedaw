// MainComponent のアプリの更新（ヘルプ → アップデートを確認、起動時の確認）。

#include "MainComponent.h"

#include "Dialogs.h"
#include "sync/SyncManager.h"

void MainComponent::checkForUpdates (bool interactive)
{
    // 起動時の自動確認は、CI でビルドしたアプリ（ビルド番号あり）で、同期サーバーを設定済みのときだけ
    if (! interactive && Updater::currentBuild() <= 0)
        return;

    if (! sync.hasCredentials())
    {
        if (interactive)
            Dialogs::showInfo ("アップデート"_ju,
                               "アップデートは同期サーバーから配信されます。\n"_ju
                               "同期 → サーバー設定… でサーバーの URL とトークンを設定してから、もう一度お試しください。"_ju);
        return;
    }

    if (updateCheckRunning.exchange (true))
        return;

    auto client = sync.makeClient();
    juce::Component::SafePointer<MainComponent> safe (this);

    juce::Thread::launch ([client, safe, interactive]
    {
        std::optional<Updater::Info> info;
        auto result = Updater::fetchLatest (client, info);

        juce::MessageManager::callAsync ([safe, interactive, result, info]
        {
            if (safe == nullptr)
                return;

            safe->updateCheckRunning = false;

            if (result.failed())
            {
                if (interactive)
                    Dialogs::showError ("アップデート"_ju, "更新を確認できませんでした: "_ju + result.getErrorMessage());
                return;
            }

            const int current = Updater::currentBuild();

            if (! info || info->build <= current)
            {
                if (interactive)
                    Dialogs::showInfo ("アップデート"_ju,
                                       ! info ? "配信されている更新はまだありません。"_ju
                                              : "最新のバージョンです（"_ju + Updater::versionText (current) + "）。"_ju);
                return;
            }

            // 自動の確認では「このバージョンをスキップ」したものは聞かない
            if (! interactive && safe->settings.getIntValue ("skippedUpdateBuild") == info->build)
                return;

            safe->offerUpdate (*info, interactive);
        });
    });
}

void MainComponent::offerUpdate (const Updater::Info& info, bool interactive)
{
    const int current = Updater::currentBuild();
    auto message = "新しいバージョンがあります。\n\n"_ju
                   + "今: "_ju + Updater::versionText (current) + "\n"
                   + "新: "_ju + Updater::versionText (info.build) + "\n";

    if (info.notes.isNotEmpty())
        message << "\n" << info.notes << "\n";

    message << "\n"_ju << "変わったファイルだけをダウンロードして、このアプリを置き換えます。"_ju;

    auto options = juce::MessageBoxOptions()
                     .withIconType (juce::MessageBoxIconType::QuestionIcon)
                     .withTitle ("アップデート"_ju)
                     .withMessage (message)
                     .withButton ("更新する"_ju)
                     .withAssociatedComponent (this);

    // 起動時の確認では「このバージョンをスキップ」も選べる
    if (! interactive)
        options = options.withButton ("このバージョンをスキップ"_ju);

    options = options.withButton ("あとで"_ju);

    juce::Component::SafePointer<MainComponent> safe (this);

    juce::AlertWindow::showAsync (options, [safe, info, interactive] (int result)
    {
        if (safe == nullptr)
            return;

        // 結果は 1, 2, …、最後のボタン（あとで）は 0
        if (result == 1)
            safe->installUpdate (info);
        else if (result == 2 && ! interactive)
            safe->settings.setValue ("skippedUpdateBuild", info.build);
    });
}

void MainComponent::installUpdate (const Updater::Info& info)
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
        return;
    }

    settings.removeValue ("skippedUpdateBuild");

    Dialogs::confirm ("アップデート"_ju,
                      Updater::versionText (info.build) + " に更新しました。\n再起動すると新しいバージョンになります。今すぐ再起動しますか？"_ju,
                      "再起動"_ju,
                      []
                      {
                          Updater::requestRelaunch();
                          juce::JUCEApplication::getInstance()->systemRequestedQuit();
                      },
                      this);
}
