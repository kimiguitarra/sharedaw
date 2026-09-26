#include <iostream>

#include "Common.h"

#include "AppPaths.h"
#include "EngineBridge.h"
#include "InstrumentLibrary.h"
#include "ProjectDocument.h"
#include "SessionGuard.h"
#include "SfizzPlugin.h"
#include "sync/SyncManager.h"
#include "collab/ProjectDiff.h"
#include "collab/ChordPlayback.h"
#include "ui/Dialogs.h"
#include "ui/MainComponent.h"
#include "ui/Theme.h"

namespace
{
    /** Tracktion Engine の設定。デバイスは Engine が自動で初期化する。 */
    struct CollabEngineBehaviour  : public te::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override   { return true; }
    };

    /** Tracktion からの UI 要求。書き出しなどの重い処理は、いまは同期実行する（進捗表示は M2 で追加）。 */
    struct CollabUIBehaviour  : public te::UIBehaviour
    {
        void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
        {
            while (task.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
            {}
        }

        void showWarningMessage (const juce::String& message) override
        {
            DBG ("Tracktion warning: " << message);
            juce::ignoreUnused (message);
        }
    };
}

//==============================================================================
class MainWindow  : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, std::unique_ptr<MainComponent> content)
        : DocumentWindow (name, Theme::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        auto* mc = content.get();
        setContentOwned (content.release(), true);

       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (mc);
       #else
        setMenuBar (mc);
       #endif

        mc->onTitleChanged = [this] (const juce::String& t) { setName (t); };

        setResizable (true, true);
        setResizeLimits (900, 600, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    ~MainWindow() override
    {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #else
        setMenuBar (nullptr);
       #endif
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

//==============================================================================
class CollabDawApplication  : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    // コマンドライン操作（--render / --sync-*）は GUI と同時に動かせるようにする
    bool moreThanOneInstanceAllowed() override             { return getCommandLineParameters().startsWith ("--"); }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        juce::PropertiesFile::Options opts;
        opts.applicationName = "CollabDAW";
        opts.filenameSuffix = ".settings";
        opts.folderName = "CollabDAW";
        opts.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (opts);

        MainComponent::applyFontScale ((float) settings->getDoubleValue ("uiScale", 1.0));

        engine = std::make_unique<te::Engine> (getApplicationName(), std::make_unique<CollabUIBehaviour>(),
                                               std::make_unique<CollabEngineBehaviour>());
        engine->getPluginManager().createBuiltInType<SfizzPlugin>();
        preferProjectSampleRate();

        library = std::make_unique<InstrumentLibrary> (AppPaths::getAssetsDir());
        document = std::make_unique<ProjectDocument>();
        bridge = std::make_unique<EngineBridge> (*engine, *document, *library);
        sync = std::make_unique<SyncManager> (*document, *settings);

        // コマンドライン: --render <プロジェクトフォルダ> <出力.wav>（動作確認・CI 用）
        if (auto args = getCommandLineParameterArray(); args.size() >= 3 && args[0] == "--render")
        {
            setApplicationReturnValue (renderProject (juce::File (args[1]), juce::File (args[2])));
            quit();
            return;
        }

        // 同期のコマンドライン操作（動作確認・スクリプト用）
        if (auto args = getCommandLineParameterArray(); ! args.isEmpty() && args[0].startsWith ("--sync-"))
        {
            setApplicationReturnValue (runSyncCommand (args));
            quit();
            return;
        }

        auto content = std::make_unique<MainComponent> (*engine, *document, *bridge, *library, *sync, *settings);
        mainComponent = content.get();
        content->getCommandManager().registerAllCommandsForTarget (this);
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), std::move (content));

        document->locationListeners.push_back ([this] { sessionGuard.markRunning (document->getProjectDir(), document->getAutosaveFile()); });
        document->locationListeners.push_back ([this] { sync->reloadForDocument(); });

        // 前回の異常終了を検知したら、自動保存からの復旧を確認する（§3.10）
        if (auto crashed = sessionGuard.findCrashedSession())
        {
            auto info = *crashed;
            auto where = info.projectDir == juce::File() ? juce::String ("（未保存のプロジェクト）"_ju) : info.projectDir.getFullPathName();

            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsYesNo (
                                              juce::MessageBoxIconType::WarningIcon, "前回は正常に終了しませんでした"_ju,
                                              "自動保存から復旧しますか？\n\n"_ju + where, "復旧する"_ju, "復旧しない"_ju),
                                          [this, info] (int result)
            {
                if (result == 1)
                {
                    try
                    {
                        document->recoverFromAutosave (info.autosaveFile, info.projectDir);
                        return;
                    }
                    catch (const std::exception& e)
                    {
                        Dialogs::showError ("復旧に失敗しました"_ju, juce::String::fromUTF8 (e.what()));
                    }
                }

                openLastProject();
            });
        }
        else
        {
            openLastProject();
        }

        sessionGuard.markRunning (document->getProjectDir(), document->getAutosaveFile());
    }

    void shutdown() override
    {
        if (document != nullptr)
            document->writeAutosave();

        mainWindow = nullptr;
        sync = nullptr;
        bridge = nullptr;
        document = nullptr;
        engine = nullptr;
        settings = nullptr;
        sessionGuard.markCleanExit();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainComponent == nullptr)
            return quit();

        mainComponent->confirmDiscardChanges ([] { juce::JUCEApplication::getInstance()->quit(); });
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    Theme::LookAndFeel lookAndFeel;
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<InstrumentLibrary> library;
    std::unique_ptr<ProjectDocument> document;
    std::unique_ptr<EngineBridge> bridge;
    std::unique_ptr<SyncManager> sync;
    std::unique_ptr<MainWindow> mainWindow;
    MainComponent* mainComponent = nullptr;
    SessionGuard sessionGuard;

    int renderProject (const juce::File& folder, const juce::File& output)
    {
        try
        {
            document->load (folder);
        }
        catch (const std::exception& e)
        {
            std::cerr << "load failed: " << e.what() << std::endl;
            return 2;
        }

        if (! bridge->renderToFile (output, collab::chordTrackEndTick (document->getProject(), document->getTempoMap())))
        {
            std::cerr << "render failed" << std::endl;
            return 3;
        }

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (output));

        if (reader == nullptr)
            return 4;

        juce::Range<float> range[2];
        reader->readMaxLevels (0, reader->lengthInSamples, range, 2);
        const float peak = juce::jmax (std::abs (range[0].getStart()), std::abs (range[0].getEnd()));
        std::cout << "rendered " << output.getFullPathName() << ": " << reader->lengthInSamples << " samples @ "
                  << reader->sampleRate << " Hz, peak " << peak << std::endl;
        return peak > 0.001f ? 0 : 5;
    }

    /**
        --sync-config <url> <token>
        --sync-register <dir>
        --sync-push <dir> [message] [--keep-locks]
        --sync-pull <dir>
        --sync-open <projectId> <parentDir>
        --sync-lock <dir> <scopeId|tempo|meter|chord|trackName> [--release|--force]
        --sync-status <dir>
    */
    int runSyncCommand (const juce::StringArray& args)
    {
        auto fail = [] (const juce::String& message)
        {
            std::cerr << "error: " << message.toStdString() << std::endl;
            return 1;
        };

        const auto command = args[0];

        if (command == "--sync-config")
        {
            if (args.size() < 3) return fail ("usage: --sync-config <url> <token>");
            sync->setCredentials (args[1], args[2]);
            auto me = sync->fetchMe();
            if (! me.ok()) return fail (me.message());
            std::cout << "ok: " << me.body.dump() << std::endl;
            return 0;
        }

        if (command == "--sync-open")
        {
            if (args.size() < 3) return fail ("usage: --sync-open <projectId> <parentDir>");
            juce::File created;
            auto r = sync->runOpenFromServer (args[1].toStdString(), juce::File (args[2]), created);
            if (r.failed()) return fail (r.getErrorMessage());
            std::cout << "opened: " << created.getFullPathName() << std::endl;
            return 0;
        }

        if (args.size() < 2)
            return fail ("usage: " + command + " <dir> ...");

        try
        {
            document->load (juce::File (args[1]));
        }
        catch (const std::exception& e)
        {
            return fail (juce::String::fromUTF8 (e.what()));
        }

        sync->reloadForDocument();
        const auto dir = document->getProjectDir();

        if (command == "--sync-register")
        {
            const auto snapshot = document->getProject();
            auto r = sync->runRegister (snapshot, dir);
            if (r.failed()) return fail (r.getErrorMessage());
            sync->applyRegistered (snapshot, sync->getMeta().baseRevision);
            std::cout << "registered: revision " << sync->getMeta().baseRevision << std::endl;
            return 0;
        }

        if (command == "--sync-status")
        {
            if (auto r = sync->fetchLocks(); r.failed()) return fail (r.getErrorMessage());
            std::cout << "linked: " << sync->isLinked() << " base: " << sync->getMeta().baseRevision << std::endl;

            for (auto& id : collab::allScopeIds (document->getProject()))
            {
                auto lock = sync->getLock (id);
                std::cout << "  " << sync->scopeName (id).toStdString() << " [" << id << "]"
                          << (lock ? " locked by " + lock->displayName.toStdString() : std::string())
                          << (sync->hasLocalChanges (id) ? " (local changes)" : "") << std::endl;
            }
            return 0;
        }

        if (command == "--sync-lock")
        {
            if (args.size() < 3) return fail ("usage: --sync-lock <dir> <scope> [--release|--force]");
            const auto& p = document->getProject();
            std::string scope = args[2].toStdString();

            if (scope == "tempo")  scope = p.tempoTrack.id;
            if (scope == "meter")  scope = p.meterTrack.id;
            if (scope == "chord")  scope = p.chordTrack.id;

            for (auto& t : p.tracks)
                if (t.name == scope)
                    scope = t.id;

            juce::Result r = args.contains ("--release") ? sync->runReleaseLock (scope, false)
                           : args.contains ("--force")   ? sync->runReleaseLock (scope, true)
                                                         : sync->runAcquireLock (scope);
            if (r.failed()) return fail (r.getErrorMessage());
            std::cout << "ok" << std::endl;
            return 0;
        }

        if (command == "--sync-push")
        {
            SyncManager::PushPlan plan;
            if (auto r = sync->fetchPushPlan (document->getProject(), plan); r.failed()) return fail (r.getErrorMessage());
            if (plan.needsPull) return fail ("pull required (head " + juce::String (plan.head) + ")");
            if (! plan.notLocked.empty()) return fail ("lock required: " + juce::String (plan.notLocked.front()));

            for (auto& c : plan.diff.changes)
                std::cout << "  " << c.scopeName << ": " << c.summary << std::endl;

            int revision = 0;
            const auto message = args.size() >= 3 && ! args[2].startsWith ("--") ? args[2] : juce::String();
            if (auto r = sync->runPush (plan, message, ! args.contains ("--keep-locks"), dir, revision); r.failed())
                return fail (r.getErrorMessage());

            sync->applyPushed (plan, revision);
            std::cout << "pushed: revision " << revision << std::endl;
            return 0;
        }

        if (command == "--sync-pull")
        {
            SyncManager::PullPreview preview;
            if (auto r = sync->fetchPullPreview (preview); r.failed()) return fail (r.getErrorMessage());

            if (preview.head == sync->getMeta().baseRevision)
            {
                std::cout << "up to date: revision " << preview.head << std::endl;
                return 0;
            }

            for (auto& c : preview.diff.changes)
                std::cout << "  " << c.scopeName << ": " << c.summary << std::endl;

            if (auto r = sync->runDownloadAudio (preview.headProject, dir); r.failed()) return fail (r.getErrorMessage());

            auto report = sync->applyPull (preview);
            std::cout << "pulled: revision " << preview.head << " kept local: " << report.keptLocal.size()
                      << " conflicts: " << report.conflicts.size() << std::endl;
            return 0;
        }

        return fail ("unknown command " + command);
    }

    void openLastProject()
    {
        auto last = juce::File (settings->getValue ("lastProjectDir"));

        if (last != juce::File() && last.getChildFile ("project.json").existsAsFile())
            mainComponent->openProjectFolder (last);
    }

    /** 初回起動時、デバイスが対応していれば 48kHz にする（プロジェクトは 48kHz 固定、§8.1）。 */
    void preferProjectSampleRate()
    {
        if (settings->getBoolValue ("sampleRateInitialised", false))
            return;

        settings->setValue ("sampleRateInitialised", true);
        auto& dm = engine->getDeviceManager().deviceManager;

        if (auto* device = dm.getCurrentAudioDevice())
        {
            if (! juce::exactlyEqual (device->getCurrentSampleRate(), 48000.0) && device->getAvailableSampleRates().contains (48000.0))
            {
                auto setup = dm.getAudioDeviceSetup();
                setup.sampleRate = 48000.0;
                dm.setAudioDeviceSetup (setup, true);
            }
        }
    }
};

START_JUCE_APPLICATION (CollabDawApplication)
