
#include "Common.h"

#include "AppPaths.h"
#include "EngineBridge.h"
#include "InstrumentLibrary.h"
#include "ProjectDocument.h"
#include "SessionGuard.h"
#include "Translations.h"
#include "SfizzPlugin.h"
#include "audio/ChannelStripPlugin.h"
#include "audio/BuiltinEffectPlugin.h"
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
#include "audio/Export.h"
#include "audio/Takes.h"
#include "audio/LoopbackDevice.h"
#include "plugins/PluginHost.h"
#include "ui/Dialogs.h"
#include "ui/MainComponent.h"
#include "ui/Theme.h"

#include <iostream>
#include <thread>
#include <optional>

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
        // Tracktion は MIDI 入力の行き先（録音中に弾いたノートの表示など）を「操作中の Edit」から調べる
        te::Edit* edit = nullptr;
        te::Edit* getCurrentlyFocusedEdit() override   { return edit; }
        te::Edit* getLastFocusedEdit() override        { return edit; }

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
        setContent (std::move (content));

        setResizable (true, true);
        setResizeLimits (900, 600, 10000, 10000);

        // DAW は画面いっぱいで使うので、最初から画面全体に広げる（最大化）
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            setBounds (display->userArea);

        setVisible (true);

       #if ! JUCE_MAC
        setFullScreen (true);   // Windows・Linux: ネイティブのタイトルバーでは「最大化」になる
       #endif
    }

    /** 中身（MainComponent）を消す。メニューは中身を参照しているので、先にメニューを外す（外さずに消すと、消えたものをメニューが触って落ちる）。 */
    void clearContent()
    {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #else
        setMenuBar (nullptr);
       #endif
        clearContentComponent();
    }

    /** 中身（MainComponent）を入れ替える（外観の切り替えで作り直すとき）。 */
    void setContent (std::unique_ptr<MainComponent> content)
    {
        clearContent();

        auto* mc = content.get();
        setBackgroundColour (Theme::background);
        setContentOwned (content.release(), false);

       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (mc);
       #else
        setMenuBar (mc, 28);
       #endif

        mc->onTitleChanged = [this] (const juce::String& t) { setName (t); };
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

        // 画面の色（ダーク / ライト）。部品を作る前に決める
        Theme::applyPalette (settings->getValue ("uiTheme", "light") != "dark");   // 既定はライト（ニューモーフィズム）
        lookAndFeel.applyColours();

        engine = std::make_unique<te::Engine> (getApplicationName(), std::make_unique<CollabUIBehaviour>(),
                                               std::make_unique<CollabEngineBehaviour>());
        engine->getPluginManager().createBuiltInType<SfizzPlugin>();
        engine->getPluginManager().createBuiltInType<CountInPlugin>();
        engine->getPluginManager().createBuiltInType<ChannelStripPlugin>();
        engine->getPluginManager().createBuiltInType<BuiltinEffectPlugin>();
        engine->getPluginManager().createBuiltInType<MasterLimiterPlugin>();
        engine->getPluginManager().createBuiltInType<HostSyncedExternalPlugin>();
        engine->getPluginManager().setUsesSeparateProcessForScanning (true);
        preferProjectSampleRate();

        library = std::make_unique<InstrumentLibrary> (AppPaths::getAssetsDir());
        document = std::make_unique<ProjectDocument>();

        // 外部プラグインのトラックの持ち主か（状態ファイルがこの PC にある）。持ち主の PC ではそのトラックをアップせず、
        // バウンスしたトラックを隠す（§3.7）
        collab::setOwnedPluginTrackCheck ([doc = document.get()] (const collab::Track& t)
        {
            return doc->hasLocation() && ! PluginHost::hasMissingState (t, doc->getProjectDir());
        });

        bridge = std::make_unique<EngineBridge> (*engine, *document, *library);

        if (auto* ui = dynamic_cast<CollabUIBehaviour*> (&engine->getUIBehaviour()))
            ui->edit = &bridge->getEdit();
        sync = std::make_unique<SyncManager> (*document, *settings);
        document->beforeSave = [this] { bridge->flushPluginStates(); };

        // コマンドラインの操作（動作確認・CI・スクリプト用）。GUI を出さずに終わる
        if (auto code = runCommandLine (getCommandLineParameterArray()))
        {
            setApplicationReturnValue (*code);
            quit();
            return;
        }

        // --switch-test <プロジェクトフォルダ> <秒>（動作確認用: 再生しながら選択トラックを切り替える。SHAREDAW_LOOPBACK_CAPTURE で出力を書き出す）
        if (auto args = getCommandLineParameterArray(); args.size() >= 3 && args[0] == "--switch-test")
        {
            installLoopbackIfRequested();
            juce::Timer::callAfterDelay (1500, [this, args] { switchTest (args); });
            return;
        }

        // --record-test <プロジェクトフォルダ> <秒> [カウントインの小節数] [開始小節]（動作確認用: 最初の入力から新しいトラックに録音する）
        if (auto args = getCommandLineParameterArray(); args.size() >= 3 && args[0] == "--record-test")
        {
            // SHAREDAW_LOOPBACK=<入力レイテンシ>,<出力レイテンシ>,<バッファ>（サンプル）なら、出力を入力に戻す仮想の機器で録音のずれを測る
            installLoopbackIfRequested();

            // 入力デバイスの一覧ができるのを待つ
            juce::Timer::callAfterDelay (1500, [this, args] { recordTest (args); });
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), createMainComponent());

        document->locationListeners.push_back ([this] { sessionGuard.markRunning (document->getProjectDir(), document->getAutosaveFile()); });
        document->locationListeners.push_back ([this] { sync->reloadForDocument(); });

        // --smoke-test [曲のフォルダ]（CI 用）: ふつうに起動して一通り操作し、外観の切り替えもしてから終わる
        if (auto args = getCommandLineParameterArray(); ! args.isEmpty() && args[0] == "--smoke-test")
        {
            installLoopbackIfRequested();   // SHAREDAW_LOOPBACK があれば、処理の重さ（途切れ）を測れる仮想の機器で
            runSmokeTest (args.size() >= 2 ? juce::File (args[1]) : juce::File());
            return;
        }

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

        collab::setOwnedPluginTrackCheck ({});   // document を参照しているので、消す前に外す

        if (document != nullptr)
            document->writeAutosave();

        mainWindow = nullptr;
        sync = nullptr;
        if (auto* ui = dynamic_cast<CollabUIBehaviour*> (&engine->getUIBehaviour()))
            ui->edit = nullptr;

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
    /** 画面の中身を作る（起動時と、外観を切り替えて作り直すとき）。 */
    std::unique_ptr<MainComponent> createMainComponent()
    {
        auto content = std::make_unique<MainComponent> (*engine, *document, *bridge, *library, *sync, *settings);
        mainComponent = content.get();
        content->getCommandManager().registerAllCommandsForTarget (this);

        // 外観（ダーク / ライト）の切り替え: 色を変えてから画面を作り直す（曲・再生はそのまま）
        content->onAppearanceChanged = [this] (bool useLight)
        {
            juce::MessageManager::callAsync ([this, useLight]
            {
                if (mainWindow == nullptr)
                    return;

                Theme::applyPalette (useLight);
                lookAndFeel.applyColours();
                mainComponent = nullptr;
                mainWindow->clearContent();   // 先に古い画面を消す（エンジンへの登録を外してから作る）
                mainWindow->setContent (createMainComponent());
                mainWindow->repaint();
            });
        };

        return content;
    }

    MainComponent* mainComponent = nullptr;
    SessionGuard sessionGuard;
    bool childProcessMode = false;

    /**
        GUI を出さずに終わるコマンドライン操作（動作確認・CI・スクリプト用）。当てはまらなければ nullopt。
          --render <曲のフォルダ> <出力.wav>               32 bit で書き出して、無音なら失敗（CI）
          --export <wav|mp3|stems|midi> <曲のフォルダ> <出力>  ファイル → 書き出し と同じ処理（CI）
          --import-audio <曲のフォルダ> <ファイル>...
          --midi-info <ファイル>                           MIDI ファイルの読み込み結果
          --scan-plugins                                   別プロセスでスキャン（落ちたものはブラックリストへ）
          --bounce <曲のフォルダ> [トラック名] / --render-status <曲のフォルダ>
          --sync-*                                         runSyncCommand を参照
    */
    std::optional<int> runCommandLine (const juce::StringArray& args)
    {
        if (args.isEmpty())
            return std::nullopt;

        const auto& name = args[0];

        if (name == "--render" && args.size() >= 3)                              return renderProject (juce::File (args[1]), juce::File (args[2]));
        if (name == "--export" && args.size() >= 4)                              return exportCommand (args[1], juce::File (args[2]), juce::File (args[3]));
        if (name == "--import-audio" && args.size() >= 3)                        return importAudioCommand (args);
        if (name == "--midi-info" && args.size() >= 2)                           return midiInfoCommand (juce::File (args[1]));
        if (name == "--scan-plugins")                                            return scanPluginsCommand();
        if ((name == "--bounce" || name == "--render-status") && args.size() >= 2) return bounceCommand (args);
        if (name.startsWith ("--sync-"))                                         return runSyncCommand (args);

        return std::nullopt;
    }

    /** コマンドライン用: 曲を開く（開けなければ理由を出して false）。 */
    bool loadForCommand (const juce::File& folder)
    {
        try
        {
            document->load (folder);
            return true;
        }
        catch (const std::exception& e)
        {
            std::cerr << "load failed: " << e.what() << std::endl;
            return false;
        }
    }

    /** 書き出した音を読み直して、長さ・レート・ピークを出す。読めなければ nullopt。 */
    static std::optional<float> printAudioSummary (const juce::File& f, const char* label)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));

        if (reader == nullptr)
        {
            std::cerr << "cannot read " << f.getFullPathName() << std::endl;
            return std::nullopt;
        }

        juce::Range<float> range[2];
        reader->readMaxLevels (0, reader->lengthInSamples, range, 2);
        const float peak = juce::jmax (std::abs (range[0].getStart()), std::abs (range[0].getEnd()),
                                       std::abs (range[1].getStart()), std::abs (range[1].getEnd()));
        std::cout << label << " " << f.getFileName() << ": " << reader->lengthInSamples << " samples @ " << reader->sampleRate
                  << " Hz, " << reader->numChannels << " ch, " << reader->bitsPerSample << " bit, peak " << peak << std::endl;
        return peak;
    }

    int midiInfoCommand (const juce::File& file)
    {
        auto r = MidiImport::read (file);

        if (! r.ok())
            std::cout << "error: " << r.error.toStdString() << std::endl;

        if (r.bpm) std::cout << "bpm " << *r.bpm << std::endl;
        if (r.meter) std::cout << "meter " << r.meter->first << "/" << r.meter->second << std::endl;

        for (auto& p : r.parts)
            std::cout << "part '" << p.name << "' ch" << p.channel << " prog" << p.program << " notes " << p.notes.size()
                      << " end " << p.endTick << " -> " << p.builtinInstrument() << std::endl;

        return r.ok() ? 0 : 1;
    }

    int scanPluginsCommand()
    {
        auto result = PluginHost::scan (*engine, nullptr);
        std::cout << "found " << result.found << " plugin(s)" << std::endl;

        for (auto& f : result.blacklisted)
            std::cout << "blacklisted: " << f << std::endl;

        return 0;
    }

    int importAudioCommand (const juce::StringArray& args)
    {
        if (! loadForCommand (juce::File (args[1])))
            return 2;

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

    LoopbackDeviceType* loopbackType = nullptr;
    struct MeasuredMidi { std::vector<collab::Tick> ticks; };
    std::shared_ptr<MeasuredMidi> measuredMidi;

    void installLoopbackIfRequested()
    {
        const auto spec = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_LOOPBACK", {});

        if (spec.isEmpty())
            return;

        const int in = spec.upToFirstOccurrenceOf (",", false, false).getIntValue();
        const int out = spec.fromFirstOccurrenceOf (",", false, false).upToFirstOccurrenceOf (",", false, false).getIntValue();
        const int buffer = juce::jmax (32, spec.fromLastOccurrenceOf (",", false, false).getIntValue());
        auto& adm = engine->getDeviceManager().deviceManager;
        auto type = std::make_unique<LoopbackDeviceType> (in, out, buffer);
        loopbackType = type.get();
        adm.addAudioDeviceType (std::move (type));
        adm.setCurrentAudioDeviceType ("Loopback", true);

        auto setup = adm.getAudioDeviceSetup();
        setup.inputDeviceName = setup.outputDeviceName = "Loopback";
        setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, 2, true);
        setup.outputChannels.clear();
        setup.outputChannels.setRange (0, 2, true);
        setup.sampleRate = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_LOOPBACK_RATE", "48000").getDoubleValue();
        setup.bufferSize = buffer;
        std::cout << "loopback: " << adm.setAudioDeviceSetup (setup, true) << " latency in " << in << " out " << out << std::endl;

        engine->getDeviceManager().createVirtualMidiDevice (EngineBridge::loopbackMidiName);

        // 有効にするのは録音の前に（有効にすると機器の一覧が作り直され、その間の録音は途中で止まる）
        juce::Timer::callAfterDelay (300, [this]
        {
            for (auto& d : engine->getDeviceManager().getMidiInDevices())
                if (d != nullptr && d->getName() == EngineBridge::loopbackMidiName)
                    d->setEnabled (true);
        });
    }

    /** 録った音・ノートが拍からどれだけずれたか（ms）を出す。 */
    void printOffsets (const char* what, const std::vector<double>& seconds)
    {
        const auto& map = document->getTempoMap();
        std::vector<double> offsets;

        for (double t : seconds)
        {
            const double tick = map.secondsToTick (t);
            const double beat = std::round (tick / collab::kPpq) * collab::kPpq;
            offsets.push_back ((t - map.tickToSeconds (beat)) * 1000.0);
        }

        if (offsets.empty())
        {
            std::cout << what << " offset: none" << std::endl;
            return;
        }

        std::sort (offsets.begin(), offsets.end());
        std::cout << what << " offset ms: median " << offsets[offsets.size() / 2] << " min " << offsets.front()
                  << " max " << offsets.back() << " (" << offsets.size() << ")" << std::endl;
    }

    /** 再生しながら、MIDI の入力先と録音待機を 0.5 秒ごとにトラックからトラックへ移す（選択トラックを切り替えたときと同じ）。 */
    void switchTest (const juce::StringArray& args)
    {
        auto finish = [this] (int code) { setApplicationReturnValue (code); quit(); };

        if (! loadForCommand (juce::File (args[1])))
            return finish (2);

        std::vector<std::string> ids;

        for (auto& t : document->getProject().tracks)
            if (t.type != collab::TrackType::bus)
                ids.push_back (t.id);

        // SHAREDAW_SWITCH=midi: キーボード（仮想 MIDI）で弾いた音が、選んだ MIDI トラックの音源だけで鳴るか確かめる
        if (juce::SystemStats::getEnvironmentVariable ("SHAREDAW_SWITCH", "1") == "midi")
            return gateTest (finish);

        const bool doSwitch = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_SWITCH", "1") != "0";
        const auto inputs = bridge->getAudioInputs();
        bridge->setPositionTick (0);
        bridge->play();
        std::cout << "playing, switching: " << (doSwitch ? "yes" : "no") << std::endl;

        auto step = std::make_shared<int> (0);
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [this, ids, inputs, doSwitch, step, tick]
        {
            if (doSwitch && ! ids.empty())
            {
                const auto& id = ids[(size_t) (*step % (int) ids.size())];
                const auto& previous = ids[(size_t) ((*step + (int) ids.size() - 1) % (int) ids.size())];
                bridge->setMidiTarget (id);

                if (! inputs.isEmpty())
                {
                    bridge->setTrackInput (previous, {});
                    bridge->setTrackInput (id, { inputs[0], {}, true, false });
                }
            }

            ++*step;
            juce::Timer::callAfterDelay (500, [t = *tick] { t(); });
        };
        juce::Timer::callAfterDelay (500, [t = *tick] { t(); });

        juce::Timer::callAfterDelay ((int) (args[2].getDoubleValue() * 1000.0), [this, finish, step]
        {
            std::cout << "switched " << *step << " times" << std::endl;
            bridge->stop();
            finish (0);
        });
    }

    void gateTest (std::function<void (int)> finish)
    {
        te::MidiInputDevice* virtualMidi = nullptr;

        for (auto& d : engine->getDeviceManager().getMidiInDevices())
            if (d != nullptr && d->getName() == EngineBridge::loopbackMidiName)
            {
                d->setEnabled (true);
                virtualMidi = d.get();
            }

        std::vector<std::pair<std::string, std::string>> midiTracks;

        for (auto& t : document->getProject().tracks)
            if (t.type == collab::TrackType::midi)
                midiTracks.emplace_back (t.id, t.name);

        if (virtualMidi == nullptr || midiTracks.empty())
        {
            std::cerr << "gate: no virtual midi or no midi tracks" << std::endl;
            return finish (3);
        }

        auto failures = std::make_shared<int> (0);
        auto step = std::make_shared<std::function<void (size_t)>>();
        *step = [this, virtualMidi, midiTracks, failures, step, finish] (size_t k)
        {
            if (k >= midiTracks.size())
            {
                std::cout << "gate: " << (*failures == 0 ? "ok" : "FAILED") << std::endl;
                return finish (*failures == 0 ? 0 : 6);
            }

            bridge->setMidiTarget (midiTracks[k].first);

            juce::Timer::callAfterDelay (300, [this, virtualMidi, midiTracks, failures, step, k]
            {
                for (auto& [id, name] : midiTracks)
                    bridge->getTrackPeakDb (id);   // ここまでのピークを捨てる

                for (int pitch : { 36, 38, 42, 48, 60 })
                    virtualMidi->handleIncomingMidiMessage (juce::MidiMessage::noteOn (k == 0 ? 10 : 1, pitch, (juce::uint8) 120), virtualMidi->getMPESourceID());

                juce::Timer::callAfterDelay (400, [this, virtualMidi, midiTracks, failures, step, k]
                {
                    std::cout << "gate: target " << midiTracks[k].second << ":";

                    for (auto& [id, name] : midiTracks)
                    {
                        const auto p = bridge->getTrackPeakDb (id);
                        const float db = juce::jmax (p.left, p.right);
                        const bool sounding = db > -60.0f;
                        std::cout << " " << name << "=" << db;

                        if (sounding != (id == midiTracks[k].first))
                            ++*failures;
                    }

                    std::cout << std::endl;

                    for (int ch : { 1, 10 })
                        virtualMidi->handleIncomingMidiMessage (juce::MidiMessage::allNotesOff (ch), virtualMidi->getMPESourceID());

                    juce::Timer::callAfterDelay (600, [step, k] { (*step) (k + 1); });
                });
            });
        };

        juce::Timer::callAfterDelay (500, [step] { (*step) (0); });
    }

    void recordTest (const juce::StringArray& args)
    {
        auto finish = [this] (int code) { setApplicationReturnValue (code); quit(); };

        if (! loadForCommand (juce::File (args[1])))
            return finish (2);

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
        track.inputChannels = 1;
        document->perform ("track", [track] (collab::Project& p) { p.tracks.push_back (track); });
        bridge->sync();
        bridge->setTrackInput (track.id, { inputs[0], {}, true, false });

        // 仮想の機器: メトロノームを鳴らし、クリックが聞こえた瞬間に MIDI のノートも送る（MIDI トラックへ録音）
        if (loopbackType != nullptr)
        {
            bridge->setMetronome (juce::SystemStats::getEnvironmentVariable ("SHAREDAW_LOOPBACK_NOMETRO", {}).isEmpty(), 0.0f);

            collab::Track midiTrack;
            midiTrack.id = collab::generateUuid();
            midiTrack.type = collab::TrackType::midi;
            midiTrack.name = "MIDI Rec";
            midiTrack.color = "#81C784";
            collab::Instrument inst;
            inst.kind = collab::Instrument::Kind::builtin;
            inst.id = collab::builtin::piano;
            inst.version = library->findLatest (inst.id)->version;
            midiTrack.instrument = inst;
            document->perform ("track", [midiTrack] (collab::Project& p) { p.tracks.push_back (midiTrack); });
            bridge->sync();

            te::MidiInputDevice* virtualMidi = nullptr;

            for (auto& d : engine->getDeviceManager().getMidiInDevices())
                if (d != nullptr && d->getName() == EngineBridge::loopbackMidiName)
                {
                    d->setEnabled (true);
                    virtualMidi = d.get();
                }

            bridge->setMidiTarget (midiTrack.id);
            std::cout << "virtual midi: " << (virtualMidi != nullptr ? "yes" : "no") << std::endl;

            loopbackType->onClickHeard = [virtualMidi] (double heardAtMs)
            {
                if (virtualMidi == nullptr)
                    return;

                // 本物の MIDI 機器と同じく、ドライバーの時刻（弾いた瞬間）を付けて送る。
                // 時刻を付けないと届いた時刻になり、スレッドの起きる遅れ（混んだ CI の macOS で 20 ms 以上）がそのままずれに出る
                std::thread ([virtualMidi, heardAtMs]
                {
                    auto waitUntil = [] (double ms) { while (juce::Time::getMillisecondCounterHiRes() < ms) juce::Thread::sleep (0); };
                    auto stamped = [] (juce::MidiMessage m, double ms) { m.setTimeStamp (ms * 0.001); return m; };
                    waitUntil (heardAtMs);
                    virtualMidi->handleIncomingMidiMessage (stamped (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), heardAtMs), virtualMidi->getMPESourceID());
                    waitUntil (heardAtMs + 60.0);
                    virtualMidi->handleIncomingMidiMessage (stamped (juce::MidiMessage::noteOff (1, 60), heardAtMs + 60.0), virtualMidi->getMPESourceID());
                }).detach();
            };

            measuredMidi = std::make_shared<MeasuredMidi>();
            bridge->onMidiRecorded = [this, m = measuredMidi] (std::vector<EngineBridge::RecordedMidi> recs)
            {
                for (auto& r : recs)
                    for (auto& n : r.notes)
                        m->ticks.push_back (n.tick);
            };
        }

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

            // 仮想の機器なら、録れた音（クリックの立ち上がり）と MIDI のノートが拍からどれだけずれたかを出す
            if (loopbackType != nullptr)
            {
                for (auto& c : clips)
                {
                    juce::AudioFormatManager formats;
                    formats.registerBasicFormats();
                    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (AudioFiles::fileForHash (document->getProjectDir(), c.clip.audioHash)));

                    if (reader == nullptr)
                        continue;

                    juce::AudioBuffer<float> audio (1, (int) reader->lengthInSamples);
                    reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, false);
                    std::vector<double> onsets;
                    int quiet = 100000;
                    const double clipStart = document->getTempoMap().tickToSeconds ((double) c.clip.startTick);

                    for (int i = (int) c.clip.sourceOffsetSamples; i < audio.getNumSamples(); ++i)
                    {
                        const float v = std::abs (audio.getSample (0, i));

                        if (v > 0.02f && quiet > (int) (reader->sampleRate * 0.1))
                            onsets.push_back (clipStart + (double) (i - c.clip.sourceOffsetSamples) / reader->sampleRate);

                        quiet = v > 0.02f ? 0 : quiet + 1;
                    }

                    printOffsets ("audio", onsets);
                }

                if (measuredMidi != nullptr)
                {
                    std::vector<double> seconds;

                    for (auto t : measuredMidi->ticks)
                        seconds.push_back (document->getTempoMap().tickToSeconds ((double) t));

                    printOffsets ("midi", seconds);
                }
            }

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

            for (auto& [id, live] : bridge->getLiveRecordings())
                std::cout << "live: " << id << " peaks " << live.peaks.size() << " notes " << live.notes.size() << std::endl;
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
        if (! loadForCommand (juce::File (args[1])))
            return 2;

        bridge->sync();

        if (args[0] == "--bounce")
        {
            bridge->flushPluginStates();

            std::vector<std::string> ids;

            for (const auto& t : document->getProject().tracks)
                if (args.size() >= 3 ? toJuce (t.name) == args[2] : collab::usesExternalPlugin (t))
                    ids.push_back (t.id);

            for (const auto& id : ids)
            {
                const auto name = document->getProject().findTrack (id)->name;
                collab::Render render;
                collab::SampleCount length = 0;

                if (auto r = bridge->bounceTrack (id, render, length); r.failed())
                {
                    std::cerr << "bounce failed: " << name << ": " << r.getErrorMessage() << std::endl;
                    return 3;
                }

                std::string bouncedId;
                document->perform ("bounce", [&] (collab::Project& p)
                {
                    bouncedId = collab::applyBounce (p, id, render, length, collab::generateUuid(), collab::generateUuid());
                });
                std::cout << "bounced " << name << " -> " << render.audioHash << " (" << document->getProject().findTrack (bouncedId)->name
                          << ", " << length << " samples)" << std::endl;
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

    int exportCommand (const juce::String& kind, const juce::File& folder, const juce::File& output)
    {
        if (! loadForCommand (folder))
            return 2;

        juce::Result r = juce::Result::fail ("unknown export kind: " + kind);
        juce::Array<juce::File> files { output };

        if (kind == "wav")    r = Export::mixdownWav (*bridge, *document, output);
        if (kind == "mp3")    r = Export::mixdownMp3 (*bridge, *document, output);
        if (kind == "midi")   r = Export::midi (*document, output);

        if (kind == "stems")
        {
            files.clear();
            r = Export::stems (*bridge, *document, output, files);
        }

        if (r.failed())
        {
            std::cerr << "export failed: " << r.getErrorMessage() << std::endl;
            return 3;
        }

        // 確かめ: 音のファイルは読み直して長さ・レート・ピークを出す。MIDI は読み直してパートを出す
        if (kind == "midi")
        {
            auto m = MidiImport::read (output);

            for (auto& p : m.parts)
                std::cout << "midi part '" << p.name << "' ch" << p.channel << " notes " << p.notes.size() << std::endl;

            return m.ok() && ! m.parts.empty() ? 0 : 4;
        }

        int status = 0;

        for (auto& f : files)
            if (! printAudioSummary (f, "exported"))
                status = 4;

        return status;
    }

    int renderProject (const juce::File& folder, const juce::File& output)
    {
        if (! loadForCommand (folder))
            return 2;

        if (! bridge->renderToFile (output, collab::chordTrackEndTick (document->getProject(), document->getTempoMap()), bridge->tailSecondsFor ({})))
        {
            std::cerr << "render failed" << std::endl;
            return 3;
        }

        const auto peak = printAudioSummary (output, "rendered");

        if (! peak)
            return 4;

        return *peak > 0.001f ? 0 : 5;   // 無音は失敗（CI で音源が読めていないことに気付く）
    }

    /**
        --sync-config <url> <token>
        --sync-register <dir>
        --sync-push <dir> [message]          この PC で変えたスコープをすべてアップする
        --sync-pull <dir> [--keep-mine]      ダウンロードする（競合はサーバーの版。--keep-mine なら自分の版）
        --sync-open <projectId> <parentDir>
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

        if (! loadForCommand (juce::File (args[1])))
            return 1;

        sync->reloadForDocument();
        const auto dir = document->getProjectDir();

        if (command == "--sync-register")
        {
            const auto snapshot = collab::withoutLocalOnlyTracks (document->getProject());
            auto r = sync->runRegister (snapshot, dir);
            if (r.failed()) return fail (r.getErrorMessage());
            sync->applyRegistered (snapshot, sync->getMeta().baseRevision);
            std::cout << "registered: revision " << sync->getMeta().baseRevision << std::endl;
            return 0;
        }

        if (command == "--sync-status")
        {
            sync->checkServerNow();
            std::cout << "linked: " << sync->isLinked() << " base: " << sync->getMeta().baseRevision << std::endl;

            for (auto& st : sync->scopeStates())
                std::cout << "  " << st.name << " [" << st.id << "]" << (st.mine ? " (local changes)" : "")
                          << (st.theirs ? " (server changes)" : "") << (st.conflict ? " (conflict)" : "") << std::endl;
            return 0;
        }

        if (command == "--sync-push")
        {
            std::set<std::string> scopes;

            if (auto* b = sync->getBase())
                for (auto& st : collab::syncStates (*b, document->getProject(), nullptr))
                    if (st.mine)
                        scopes.insert (st.id);

            SyncManager::UploadPlan plan;
            if (auto r = sync->fetchUploadPlan (document->getProject(), scopes, plan); r.failed()) return fail (r.getErrorMessage());
            if (plan.needsDownload) return fail ("download required (head " + juce::String (plan.head) + ")");
            if (! plan.staleRenders.empty()) return fail ("bounce required: " + juce::String (plan.staleRenders.front()));

            for (auto& c : plan.diff.changes)
                std::cout << "  " << c.scopeName << ": " << c.summary << std::endl;

            int revision = 0;
            const auto message = args.size() >= 3 && ! args[2].startsWith ("--") ? args[2] : juce::String();
            if (auto r = sync->runUpload (plan, message, dir, revision); r.failed())
                return fail (r.getErrorMessage());

            sync->applyUploaded (plan, revision);
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

            std::map<std::string, collab::Resolution> choices;
            int conflicts = 0;

            if (auto* b = sync->getBase())
                for (auto& st : collab::syncStates (*b, document->getProject(), &preview.headProject))
                    if (st.conflict)
                    {
                        ++conflicts;
                        choices[st.id] = args.contains ("--keep-mine") ? collab::Resolution::mine : collab::Resolution::theirs;
                    }

            sync->applyDownload (preview, choices);
            std::cout << "pulled: revision " << preview.head << " conflicts: " << conflicts << std::endl;
            return 0;
        }

        return fail ("unknown command " + command);
    }

    /** 起動時: どの曲をやるか選ぶ画面を出す（サーバーとこの PC の状況が見える）。 */
    void runSmokeTest (const juce::File& project)
    {
        std::cout << "smoke: started (" << getApplicationVersion() << ")" << std::endl;

        mainComponent->runSmokeSteps (project, [this]
        {
            // 外観を切り替える（画面を作り直す）→ 戻す → 終わる
            auto toggleTheme = [this] (bool light)
            {
                std::cout << "smoke: switch theme to " << (light ? "light" : "dark") << std::endl;
                mainComponent->onAppearanceChanged (light);
            };

            const bool light = Theme::light;
            toggleTheme (! light);

            juce::Timer::callAfterDelay (1500, [this, toggleTheme, light]
            {
                toggleTheme (light);

                juce::Timer::callAfterDelay (1500, [this]
                {
                    std::cout << "SMOKE TEST PASSED" << std::endl;
                    setApplicationReturnValue (0);
                    quit();
                });
            });
        });
    }

    void openLastProject()
    {
        mainComponent->showProjectPicker();
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
                setup.sampleRate = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_LOOPBACK_RATE", "48000").getDoubleValue();
                dm.setAudioDeviceSetup (setup, true);
            }
        }
    }
};

START_JUCE_APPLICATION (ShareDawApplication)
