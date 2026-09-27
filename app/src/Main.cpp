#include <iostream>

#include "Common.h"

#include "AppPaths.h"
#include "EngineBridge.h"
#include "InstrumentLibrary.h"
#include "ProjectDocument.h"
#include "SessionGuard.h"
#include "Translations.h"
#include "SfizzPlugin.h"
#include "audio/ChannelStripPlugin.h"
#include "audio/MasterLimiterPlugin.h"
#include "update/Updater.h"
#include "audio/MidiImport.h"
#include "audio/CountInPlugin.h"
#include "sync/SyncManager.h"
#include "collab/ProjectDiff.h"
#include "collab/ChordPlayback.h"
#include "collab/ClipEditing.h"
#include "collab/Render.h"
#include "collab/Uuid.h"
#include "audio/AudioFiles.h"
#include "audio/Takes.h"
#include "plugins/PluginHost.h"
#include "ui/Dialogs.h"
#include "ui/MainComponent.h"
#include "ui/Theme.h"

namespace
{
    /** Tracktion Engine の設定。デバイスは Engine が自動で初期化する。 */
    struct CollabEngineBehaviour  : public te::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override   { return true; }

        // プラグインのスキャンは別プロセスで行う（§3.4: クラッシュしても本体は落ちない）
        bool canScanPluginsOutOfProcess() override    { return true; }
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
class ShareDawApplication  : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    // コマンドライン操作（--render / --sync-*）は GUI と同時に動かせるようにする
    bool moreThanOneInstanceAllowed() override             { return getCommandLineParameters().isNotEmpty(); }

    void initialise (const juce::String& commandLine) override
    {
        // プラグインのスキャン用の子プロセスとして起動された場合（§3.4: スキャンは別プロセス）
        if (te::PluginManager::startChildProcessPluginScan (commandLine))
        {
            childProcessMode = true;
            return;
        }

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        installJapaneseTranslations();

        juce::PropertiesFile::Options opts;
        opts.applicationName = "ShareDAW";
        opts.filenameSuffix = ".settings";
        opts.folderName = "ShareDAW";
        opts.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (opts);

        MainComponent::applyFontScale ((float) settings->getDoubleValue ("uiScale", 1.0));

        engine = std::make_unique<te::Engine> (getApplicationName(), std::make_unique<CollabUIBehaviour>(),
                                               std::make_unique<CollabEngineBehaviour>());
        engine->getPluginManager().createBuiltInType<SfizzPlugin>();
        engine->getPluginManager().createBuiltInType<CountInPlugin>();
        engine->getPluginManager().createBuiltInType<ChannelStripPlugin>();
        engine->getPluginManager().createBuiltInType<MasterLimiterPlugin>();
        engine->getPluginManager().setUsesSeparateProcessForScanning (true);
        preferProjectSampleRate();

        library = std::make_unique<InstrumentLibrary> (AppPaths::getAssetsDir());
        document = std::make_unique<ProjectDocument>();
        bridge = std::make_unique<EngineBridge> (*engine, *document, *library);
        sync = std::make_unique<SyncManager> (*document, *settings);
        document->beforeSave = [this] { bridge->flushPluginStates(); };

        // コマンドライン: --render <プロジェクトフォルダ> <出力.wav>（動作確認・CI 用）
        if (auto args = getCommandLineParameterArray(); args.size() >= 3 && args[0] == "--render")
        {
            setApplicationReturnValue (renderProject (juce::File (args[1]), juce::File (args[2])));
            quit();
            return;
        }

        // --import-audio <プロジェクトフォルダ> <ファイル>...（動作確認用）
        if (auto args = getCommandLineParameterArray(); args.size() >= 3 && args[0] == "--import-audio")
        {
            setApplicationReturnValue (importAudioCommand (args));
            quit();
            return;
        }

        // --midi-info <ファイル>（動作確認用: MIDI ファイルの読み込み結果を表示する）
        if (auto args = getCommandLineParameterArray(); args.size() >= 2 && args[0] == "--midi-info")
        {
            auto r = MidiImport::read (juce::File (args[1]));

            if (! r.ok())
                std::cout << "error: " << r.error.toStdString() << std::endl;

            if (r.bpm) std::cout << "bpm " << *r.bpm << std::endl;
            if (r.meter) std::cout << "meter " << r.meter->first << "/" << r.meter->second << std::endl;

            for (auto& p : r.parts)
                std::cout << "part '" << p.name << "' ch" << p.channel << " prog" << p.program << " notes " << p.notes.size()
                          << " end " << p.endTick << " -> " << p.builtinInstrument() << std::endl;

            setApplicationReturnValue (r.ok() ? 0 : 1);
            quit();
            return;
        }

        // --scan-plugins（動作確認用: 別プロセスでスキャンし、クラッシュしたものはブラックリストへ）
        if (auto args = getCommandLineParameterArray(); ! args.isEmpty() && args[0] == "--scan-plugins")
        {
            auto result = PluginHost::scan (*engine, nullptr);
            std::cout << "found " << result.found << " plugin(s)" << std::endl;

            for (auto& f : result.blacklisted)
                std::cout << "blacklisted: " << f << std::endl;

            setApplicationReturnValue (0);
            quit();
            return;
        }

        // --bounce <プロジェクトフォルダ> [トラック名]  /  --render-status <プロジェクトフォルダ>（動作確認用）
        if (auto args = getCommandLineParameterArray(); args.size() >= 2 && (args[0] == "--bounce" || args[0] == "--render-status"))
        {
            setApplicationReturnValue (bounceCommand (args));
            quit();
            return;
        }

        // --record-test <プロジェクトフォルダ> <秒> [カウントインの小節数] [開始小節]（動作確認用: 最初の入力から新しいトラックに録音する）
        if (auto args = getCommandLineParameterArray(); args.size() >= 3 && args[0] == "--record-test")
        {
            // 入力デバイスの一覧ができるのを待つ
            juce::Timer::callAfterDelay (1000, [this, args] { recordTest (args); });
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
        if (childProcessMode)
            return;

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
        Updater::relaunchIfRequested();
    }

    void systemRequestedQuit() override
    {
        if (childProcessMode)
            return quit();

        if (mainComponent == nullptr)
            return quit();

        mainComponent->confirmDiscardChanges ([] { juce::JUCEApplication::getInstance()->quit(); });
    }

    void anotherInstanceStarted (const juce::String&) override {}

    void getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info) override
    {
        JUCEApplication::getCommandInfo (id, info);

        if (id == juce::StandardApplicationCommandIDs::quit)
            info.shortName = "終了"_ju;
    }

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
    bool childProcessMode = false;

    int importAudioCommand (const juce::StringArray& args)
    {
        try
        {
            document->load (juce::File (args[1]));
        }
        catch (const std::exception& e)
        {
            std::cerr << "load failed: " << e.what() << std::endl;
            return 2;
        }

        collab::Track track;
        track.id = collab::generateUuid();
        track.type = collab::TrackType::audio;
        track.name = "Audio";

        collab::Tick tick = 0;

        for (int i = 2; i < args.size(); ++i)
        {
            AudioFiles::Imported im;

            if (auto r = AudioFiles::importFile (juce::File (args[i]), document->getProjectDir().getChildFile ("audio"), im); r.failed())
            {
                std::cerr << r.getErrorMessage() << std::endl;
                return 3;
            }

            collab::AudioClip c;
            c.id = collab::generateUuid();
            c.startTick = tick;
            c.audioHash = im.hash;
            c.displayName = toStd (im.displayName);
            c.lengthSamples = im.lengthSamples;
            c.fadeOutSamples = 2400;
            track.audioClips.push_back (c);
            tick = collab::audioClipEndTick (c, document->getTempoMap());
            std::cout << "imported " << im.hash << " " << im.lengthSamples << " samples" << std::endl;
        }

        document->perform ("import", [track] (collab::Project& p) { p.tracks.push_back (track); });
        return document->save().wasOk() ? 0 : 4;
    }

    void recordTest (const juce::StringArray& args)
    {
        auto finish = [this] (int code) { setApplicationReturnValue (code); quit(); };

        try
        {
            document->load (juce::File (args[1]));
        }
        catch (const std::exception& e)
        {
            std::cerr << "load failed: " << e.what() << std::endl;
            return finish (2);
        }

        if (auto* device = engine->getDeviceManager().deviceManager.getCurrentAudioDevice())
            std::cout << "device: " << device->getTypeName() << " / " << device->getName()
                      << " in " << device->getActiveInputChannels().countNumberOfSetBits()
                      << " of " << device->getInputChannelNames().size() << std::endl;

        for (int i = 0; i < engine->getDeviceManager().getNumWaveInDevices(); ++i)
            if (auto* w = engine->getDeviceManager().getWaveInDevice (i); w != nullptr && i < 4)
                std::cout << "wave in: " << w->getName() << (w->isEnabled() ? " (enabled)" : "") << std::endl;

        const auto inputs = bridge->getAudioInputs();
        std::cout << "inputs: " << inputs.joinIntoString (", ") << std::endl;

        if (inputs.isEmpty())
            return finish (3);

        collab::Track track;
        track.id = collab::generateUuid();
        track.type = collab::TrackType::audio;
        track.name = "Rec";
        track.color = "#4FC3F7";
        document->perform ("track", [track] (collab::Project& p) { p.tracks.push_back (track); });
        bridge->sync();
        bridge->setTrackInput (track.id, { inputs[0], true, false });

        bridge->onRecordingFinished = [this, finish] (std::vector<EngineBridge::RecordedTake> takes)
        {
            for (auto& t : takes)
                std::cout << "take: " << t.file.getFullPathName() << " start " << t.startSeconds
                          << " offset " << t.offsetSeconds << " length " << t.lengthSeconds << std::endl;

            std::vector<Takes::Clip> clips;
            auto r = Takes::import (takes, document->getProjectDir(), document->getTempoMap(), clips);
            Takes::addToProject (*document, clips);

            for (auto& c : clips)
                std::cout << "clip: tick " << c.clip.startTick << " offset " << c.clip.sourceOffsetSamples
                          << " length " << c.clip.lengthSamples << " hash " << c.clip.audioHash << std::endl;

            finish (r.wasOk() && document->save().wasOk() && ! clips.empty() ? 0 : 4);
        };

        const int countIn = args.size() >= 4 ? args[3].getIntValue() : 0;

        if (args.size() >= 5)
            bridge->setPositionTick ((double) document->getTempoMap().barToTick (args[4].getIntValue()));

        if (auto r = bridge->startRecording (countIn); r.failed())
        {
            std::cerr << "record failed: " << r.getErrorMessage() << std::endl;
            return finish (5);
        }

        std::cout << "recording..." << std::endl;
        juce::Timer::callAfterDelay ((int) (args[2].getDoubleValue() * 1000.0), [this]
        {
            std::cout << "stop at " << bridge->getPositionSeconds() << " s" << std::endl;
            bridge->stop();
        });

        // 取り込みが呼ばれなければ終わらせる
        juce::Timer::callAfterDelay ((int) (args[2].getDoubleValue() * 1000.0) + 10000, [finish]
        {
            std::cerr << "no take" << std::endl;
            finish (6);
        });
    }

    int bounceCommand (const juce::StringArray& args)
    {
        try
        {
            document->load (juce::File (args[1]));
        }
        catch (const std::exception& e)
        {
            std::cerr << "load failed: " << e.what() << std::endl;
            return 2;
        }

        bridge->sync();

        if (args[0] == "--bounce")
        {
            bridge->flushPluginStates();

            for (const auto& t : document->getProject().tracks)
            {
                if (args.size() >= 3 ? toJuce (t.name) != args[2] : ! collab::usesExternalPlugin (t))
                    continue;

                collab::Render render;

                if (auto r = bridge->bounceTrack (t.id, render); r.failed())
                {
                    std::cerr << "bounce failed: " << t.name << ": " << r.getErrorMessage() << std::endl;
                    return 3;
                }

                const auto id = t.id;
                document->perform ("bounce", [id, render] (collab::Project& p)
                {
                    if (auto* track = p.findTrack (id))
                        track->render = render;
                });
                std::cout << "bounced " << t.name << " -> " << render.audioHash << std::endl;
            }

            if (auto r = document->save(); r.failed())
            {
                std::cerr << "save failed: " << r.getErrorMessage() << std::endl;
                return 4;
            }
        }

        for (const auto& t : document->getProject().tracks)
        {
            static const char* names[] = { "notNeeded", "missing", "stale", "upToDate" };
            const auto status = collab::renderStatus (t, bridge->trackFingerprint (t));
            std::cout << t.name << ": " << names[(int) status]
                      << (bridge->isPlayingRender (t.id) ? " (playing render)" : "") << std::endl;
        }

        return 0;
    }

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
            if (scope == "marker") scope = p.markerTrack.id;
            if (scope == "master") scope = p.master.id;
            if (scope == "key")    scope = p.keyTrack.id;

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
            if (! plan.staleRenders.empty()) return fail ("bounce required: " + juce::String (plan.staleRenders.front()));

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

START_JUCE_APPLICATION (ShareDawApplication)
