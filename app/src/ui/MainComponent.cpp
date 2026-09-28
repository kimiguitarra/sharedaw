#include "MainComponent.h"

#include "Dialogs.h"
#include "ChannelStripEditor.h"
#include "MarkerLane.h"
#include "MidiInputPanel.h"
#include "MasterPanel.h"
#include "ProjectPicker.h"
#include "MixerView.h"
#include "SyncUI.h"
#include "Theme.h"
#include "collab/ChordPlayback.h"
#include "collab/ClipEditing.h"
#include "collab/MasterDsp.h"
#include "collab/Uuid.h"
#include "audio/Takes.h"
#include "sync/SyncManager.h"

namespace
{
    enum Commands
    {
        cmdNew = 0x2000, cmdOpen, cmdSave, cmdUndo, cmdRedo, cmdDelete, cmdSelectAll, cmdDuplicate,
        cmdPlay, cmdToStart, cmdLoop, cmdMetronome, cmdQuantise,
        cmdAddDrums, cmdAddBass, cmdAddPiano, cmdAddEPiano,
        cmdAudioSettings, cmdCredits, cmdAbout, cmdCheckUpdate,
        cmdFont100, cmdFont125, cmdFont150, cmdFont175, cmdFont200,
        cmdSyncSettings, cmdSyncRegister, cmdSyncOpen, cmdSyncPull, cmdSyncPush, cmdSyncHistory,
        cmdAddAudioTrack, cmdImportAudio, cmdImportMidi, cmdExportMixdown, cmdSplit, cmdMuteTrack, cmdSoloTrack, cmdPlugins, cmdArmTrack, cmdTrackHeight,
        cmdRecord, cmdCountIn0, cmdCountIn1, cmdCountIn2,
        cmdToolSelect, cmdToolPencil, cmdModeCubase, cmdModeStudioOne, cmdMixer, cmdMaster, cmdLoopToSelection,
        cmdStop, cmdZoomIn, cmdZoomOut, cmdSnap, cmdAutoScroll, cmdAddMarker,
        cmdMarker1, cmdMarker2, cmdMarker3, cmdMarker4, cmdMarker5, cmdMarker6, cmdMarker7, cmdMarker8, cmdMarker9,
        cmdToolSplit, cmdCopy, cmdCut, cmdPaste, cmdNudgeLeft, cmdNudgeRight,
        cmdForward, cmdRewind, cmdShortcuts, cmdSyncPanel, cmdSyncCreate, cmdToLoopStart, cmdToLoopEnd, cmdInspector
    };

    constexpr float fontScales[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
    constexpr int toolbarHeight = 42;
    constexpr int transportHeight = 48;
}

MainComponent::MainComponent (te::Engine& e, ProjectDocument& d, EngineBridge& b,
                              const InstrumentLibrary& lib, SyncManager& s, juce::PropertiesFile& props)
    : engine (e), document (d), bridge (b), library (lib), sync (s), settings (props)
{
    addAndMakeVisible (toolbar);
    addAndMakeVisible (transport);
    addAndMakeVisible (timeline);
    addAndMakeVisible (pianoRoll);
    addChildComponent (syncPanel);
    addChildComponent (toast);

    syncPanel.onToggle = [this] { toggleSyncPanel(); };
    syncPanel.onRegister = [this] { registerProject(); };
    syncPanel.onServerSettings = [this] { showServerSettings(); };
    syncPanel.onOpenPicker = [this] { showProjectPicker(); };
    syncPanel.onDownload = [this] (const std::map<std::string, collab::Resolution>& choices) { downloadWithChoices (choices, false); };
    syncPanel.onUpload = [this] (const std::set<std::string>& excluded, const juce::String& message,
                                 const std::map<std::string, collab::Resolution>& choices) { uploadFromPanel (excluded, message, choices); };
    syncPanel.onJump = [this] (const collab::Change& c) { jumpTo (c); };
    sync.onIncomingRevisions = [this] (const std::vector<SyncManager::RevisionInfo>& revs) { onIncomingRevisions (revs); };
    syncPanel.setVisible (true);   // 畳むと右端の細い帯になる

    resizer = std::make_unique<juce::StretchableLayoutResizerBar> (&layout, 1, false);
    addAndMakeVisible (*resizer);

    layout.setItemLayout (0, 120, -1.0, -0.55);   // タイムライン
    layout.setItemLayout (1, 6, 6, 6);            // 仕切り
    layout.setItemLayout (2, 150, -1.0, -0.45);   // ピアノロール

    audioCache.onThumbnailChanged = [this] { timeline.repaint(); };

    addChildComponent (inspector);
    inspector.setVisible (settings.getBoolValue ("inspectorVisible", true));

    // 上の段（拍子〜マーカー）の並びはこの PC の設定
    {
        auto saved = juce::StringArray::fromTokens (settings.getValue ("laneOrder"), ",", {});
        std::vector<std::string> order;

        for (auto& k : saved)
            order.push_back (k.toStdString());

        auto sorted = order, defaults = state.laneOrder;
        std::sort (sorted.begin(), sorted.end());
        std::sort (defaults.begin(), defaults.end());

        if (sorted == defaults)   // 5 つそろっているときだけ使う（古い・壊れた設定は無視）
            state.laneOrder = order;
    }

    timeline.onLaneOrderChanged = [this]
    {
        juce::StringArray keys;

        for (auto& k : state.laneOrder)
            keys.add (k);

        settings.setValue ("laneOrder", keys.joinIntoString (","));
        settings.saveIfNeeded();
    };

    // 外部プラグインのエディタ
    ctx.openPluginEditor = [this] (const std::string& trackId, const std::string& effectId)
    {
        if (auto* plugin = bridge.getExternalPlugin (trackId, effectId))
            pluginWindows.show (*plugin, plugin->getName());
        else if (bridge.isPlayingRender (trackId))
            Dialogs::showInfo ("プラグイン"_ju, "この環境ではプラグインを鳴らせないため、バウンスした音で再生しています。"_ju);
    };
    bridge.onPluginRemoved = [this] (te::Plugin* p) { pluginWindows.closeFor (p); };
    timeline.onOpenClip = [this] { pianoRoll.focusEditor(); };

    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);

    document.addChangeListener (this);
    state.addChangeListener (this);
    sync.addChangeListener (this);
    ctx.toggleRecord = [this] { toggleRecord(); };

    state.countInBars = juce::jlimit (0, 2, settings.getIntValue ("countInBars", 1));
    state.mode = settings.getValue ("operationMode") == "studioOne" ? OperationMode::studioOne : OperationMode::cubase;
    state.masterVolumeDb = (float) settings.getDoubleValue ("masterVolumeDb", 0.0);
    bridge.setMasterVolumeDb (state.masterVolumeDb);
    commandManager.getKeyMappings()->resetToDefaultMappings();
    ctx.addTrackMenu = [this] { return addTrackMenu(); };
    ctx.openChannelStrip = [this] (const std::string& id, bool compressor) { openChannelStrip (id, compressor); };
    ctx.openMaster = [this] { openMaster(); };
    bridge.onRecordingFinished = [this] (std::vector<EngineBridge::RecordedTake> takes) { importTakes (std::move (takes)); };
    bridge.onMidiRecorded = [this] (std::vector<EngineBridge::RecordedMidi> recs) { importMidiRecording (std::move (recs)); };
    engine.getDeviceManager().deviceManager.addChangeListener (this);
    applyLatencyOffset();

    // 画面の下に文字の行は出さない（ボタンの並びが崩れる）。困ったことだけダイアログで知らせる
    if (! library.getLoadErrors().isEmpty())
        Dialogs::showError ("内蔵音源の読み込みエラー"_ju, library.getLoadErrors().joinIntoString ("\n"));
    else if (library.getAll().empty())
        Dialogs::showError ("内蔵音源が見つかりません"_ju, "内蔵音源（assets フォルダ）が見つかりません。音が鳴りません。"_ju);

    updateTitle();
    startTimerHz (30);
    setSize (1400, 860);

    // 前回の更新の後片付けと、更新の確認（起動が落ち着いてから）
    juce::Timer::callAfterDelay (4000, [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        Updater::cleanUpPreviousUpdate();

        if (safe != nullptr)
            safe->checkForUpdates (false);
    });
}

MainComponent::~MainComponent()
{
    eqWindow = nullptr;
    compWindow = nullptr;
    mixerWindow = nullptr;
    bridge.onPluginRemoved = nullptr;
    bridge.onRecordingFinished = nullptr;
    bridge.onMidiRecorded = nullptr;
    engine.getDeviceManager().deviceManager.removeChangeListener (this);
    pluginWindows.closeAll();
    sync.onIncomingRevisions = nullptr;
    sync.removeChangeListener (this);
    document.removeChangeListener (this);
    state.removeChangeListener (this);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    // Cubase と同じく、上にツールバー、下にトランスポート
    toolbar.setBounds (area.removeFromTop (toolbarHeight));
    transport.setBounds (area.removeFromBottom (transportHeight));
    transport.onMixer = [this] { toggleMixer(); };

    syncPanel.setBounds (area.removeFromRight (juce::jmin (syncPanel.getPreferredWidth(), area.getWidth() / 2)));

    // 左にインスペクター（選択中のトラックのチャンネルストリップ。Alt+I で表示 / 非表示）
    if (inspector.isVisible())
        inspector.setBounds (area.removeFromLeft (Inspector::preferredWidth));

    toast.setTopLeftPosition (area.getRight() - toast.getWidth() - 12, area.getBottom() - toast.getHeight() - 12);

    juce::Component* comps[] = { &timeline, resizer.get(), &pianoRoll };
    layout.layOutComponents (comps, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);
}

void MainComponent::setStatus (const juce::String& text)
{
    // 下の文字の行はなくした（保存しました等は出さない）。調べるときのためにログにだけ残す
    DBG (text);
    juce::ignoreUnused (text);
}

void MainComponent::applyFontScale (float scale)
{
    juce::Desktop::getInstance().setGlobalScaleFactor (juce::jlimit (1.0f, 2.0f, scale));
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getDeviceManager().deviceManager)
    {
        applyLatencyOffset();
        return;
    }

    if (source == &sync)
    {
        updateTitle();
        timeline.repaint();
        state.changed();   // トラックヘッダーのロック表示を更新
        return;
    }

    if (source == &document)
    {
        updateTitle();
        commandManager.commandStatusChanged();

        // 削除されたトラック・クリップの選択を外す
        if (ctx.selectedTrack() == nullptr && ! state.selectedTrackId.empty())
        {
            state.selectedTrackId = {};
            state.selectClip ({});
            state.changed();
        }
        else if (ctx.selectedClip() == nullptr && ! state.selectedClipId.empty())
        {
            state.selectClip ({});
            state.changed();
        }
    }

    // ループ・メトロノームの状態をエンジンへ
    if (state.loopEnabled != lastLoop || state.loopStart != lastLoopStart || state.loopEnd != lastLoopEnd || source == &document)
    {
        lastLoop = state.loopEnabled;
        lastLoopStart = state.loopStart;
        lastLoopEnd = state.loopEnd;
        bridge.setLoop (state.loopEnabled, state.loopStart, state.loopEnd);
    }

    if (state.metronomeEnabled != lastMetronome || ! juce::exactlyEqual (state.metronomeVolumeDb, lastMetronomeDb))
    {
        lastMetronome = state.metronomeEnabled;
        lastMetronomeDb = state.metronomeVolumeDb;
        bridge.setMetronome (state.metronomeEnabled, state.metronomeVolumeDb);
    }

    // MIDI キーボードは録音待機の MIDI トラック（なければ選択中の MIDI トラック）で鳴らす
    {
        auto* t = midiRecordTarget();
        bridge.setMidiTarget (t != nullptr ? t->id : std::string());
    }

    if (! juce::exactlyEqual (state.masterVolumeDb, bridge.getMasterVolumeDb()))
    {
        bridge.setMasterVolumeDb (state.masterVolumeDb);
        settings.setValue ("masterVolumeDb", state.masterVolumeDb);
    }
}

void MainComponent::timerCallback()
{
    bridge.pollMidiActivity();
    const bool playing = bridge.isPlaying();
    const double tick = bridge.getPositionTick();
    state.playheadTick = tick;

    transport.updatePosition (tick, bridge.getPositionSeconds(), playing);
    toolbar.update();
    timeline.setPlayheadTick (tick);
    pianoRoll.setPlayheadTick (tick);

    if (playing && state.autoScroll)
    {
        timeline.followPlayhead (tick);
        pianoRoll.followPlayhead (tick);
    }
}

void MainComponent::updateTitle()
{
    auto title = toJuce (document.getProject().name);

    if (document.isDirty())
        title = "* " + title;

    title << " - ShareDAW";

    if (sync.isLinked())
        title << "  (rev " << sync.getMeta().baseRevision << ", " << sync.getMeta().userName << ")";

    if (document.hasLocation())
        title << "  [" << document.getProjectDir().getFullPathName() << "]";

    if (onTitleChanged)
        onTitleChanged (title);
}

//==============================================================================
void MainComponent::confirmDiscardChanges (std::function<void()> onDone)
{
    if (! document.isDirty())
        return onDone();

    Dialogs::askSaveChanges ("「"_ju + toJuce (document.getProject().name) + "」の変更を保存しますか？"_ju,
                             [this, onDone] (int result)
    {
        if (result == 1)
            saveProject ([onDone] (bool ok) { if (ok) onDone(); });
        else if (result == 2)
            onDone();
    });
}

void MainComponent::newProject()
{
    confirmDiscardChanges ([this]
    {
        Dialogs::askText ("新規プロジェクト"_ju, "曲名"_ju, "新しい曲"_ju, [this] (const juce::String& name)
        {
            bridge.stop();
            document.newProject (name.isEmpty() ? juce::String ("無題"_ju) : name);
            state.selectedTrackId = {};
            state.selectClip ({});
            state.timeline.scrollTick = 0;
            state.changed();
            bridge.returnToStart();
            setStatus ("新規プロジェクトを作成しました（最初の保存で保存先を選びます）"_ju);
        });
    });
}

void MainComponent::openProject()
{
    confirmDiscardChanges ([this]
    {
        chooser = std::make_unique<juce::FileChooser> ("プロジェクトを開く（project.json を選択）"_ju,
                                                       juce::File (settings.getValue ("lastProjectDir")).getParentDirectory(),
                                                       "project.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File())
                openProjectFolder (file.getParentDirectory());
        });
    });
}

void MainComponent::openProjectFolder (const juce::File& folder)
{
    try
    {
        bridge.stop();
        document.load (folder);
        state.selectedTrackId = {};
            state.selectClip ({});
        state.timeline.scrollTick = 0;
        state.changed();
        bridge.returnToStart();
        settings.setValue ("lastProjectDir", folder.getFullPathName());
        ProjectPicker::remember (settings, folder);
        setStatus ("開きました: "_ju + folder.getFullPathName());
    }
    catch (const std::exception& e)
    {
        Dialogs::showError ("プロジェクトを開けません"_ju, juce::String::fromUTF8 (e.what()));
    }
}

void MainComponent::saveProject (std::function<void (bool)> onDone)
{
    if (document.hasLocation())
    {
        auto r = document.save();

        if (r.failed())
            Dialogs::showError ("保存に失敗しました"_ju, r.getErrorMessage());
        else
            setStatus ("保存しました: "_ju + document.getProjectFile().getFullPathName());

        if (onDone)
            onDone (r.wasOk());

        return;
    }

    chooser = std::make_unique<juce::FileChooser> ("プロジェクトを保存するフォルダを選択（中に「曲名」のフォルダを作ります）"_ju,
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, onDone] (const juce::FileChooser& fc)
    {
        auto dir = fc.getResult();

        if (dir == juce::File())
        {
            if (onDone) onDone (false);
            return;
        }

        auto r = document.saveNew (dir);

        if (r.failed())
            Dialogs::showError ("保存に失敗しました"_ju, r.getErrorMessage());
        else
        {
            settings.setValue ("lastProjectDir", document.getProjectDir().getFullPathName());
            ProjectPicker::remember (settings, document.getProjectDir());
            setStatus ("保存しました: "_ju + document.getProjectFile().getFullPathName());
        }

        if (onDone)
            onDone (r.wasOk());
    });
}

void MainComponent::importMidi()
{
    chooser = std::make_unique<juce::FileChooser> ("MIDI ファイルを読み込む"_ju, juce::File(), "*.mid;*.midi;*.smf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
    {
        auto files = fc.getResults();

        if (files.isEmpty())
            return;

        // 選択中の MIDI トラックの、再生位置（小節の頭）に置く
        const auto& map = document.getTempoMap();
        const auto tick = map.barToTick (map.tickToBar ((collab::Tick) state.playheadTick));
        std::string trackId;

        if (auto* t = ctx.selectedTrack(); t != nullptr && t->type == collab::TrackType::midi)
            trackId = t->id;

        ctx.importMidiFiles (files, trackId, tick);
    });
}

void MainComponent::exportMixdown()
{
    // 既定の保存先: プロジェクトのフォルダの横に「プロジェクト名.wav」
    const auto name = toJuce (document.getProject().name).trim();
    const auto dir = document.hasLocation() ? document.getProjectDir().getParentDirectory()
                                            : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("ミックスダウンを書き出す"_ju,
                                                   dir.getChildFile (juce::File::createLegalFileName (name.isEmpty() ? juce::String ("mixdown") : name) + ".wav"),
                                                   "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == juce::File())
            return;

        file = file.withFileExtension ("wav");

        // 曲の最後まで（リバーブなどの余韻に 2 秒）。マスターのリミッターは含み、この PC のマスター音量・メトロノームは含まない
        const auto end = collab::chordTrackEndTick (document.getProject(), document.getTempoMap());
        juce::MouseCursor::showWaitCursor();
        const bool ok = bridge.renderToFile (file, end, 2.0, 24);
        juce::MouseCursor::hideWaitCursor();

        if (! ok)
            return Dialogs::showError ("書き出し"_ju, "ミックスダウンを書き出せませんでした。"_ju);

        // 書き出した音のラウドネスとピークを測って知らせる
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        juce::String summary;

        if (reader != nullptr)
        {
            collab::LoudnessBlocks blocks;
            blocks.prepare (reader->sampleRate);
            collab::LoudnessStats stats;
            std::vector<double> out;
            juce::AudioBuffer<float> buffer (2, 48000);
            float peak = 0.0f;

            for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += buffer.getNumSamples())
            {
                const int n = (int) juce::jmin ((juce::int64) buffer.getNumSamples(), reader->lengthInSamples - pos);
                reader->read (&buffer, 0, n, pos, true, true);
                peak = juce::jmax (peak, buffer.getMagnitude (0, n));
                out.clear();
                const float* ch[2] = { buffer.getReadPointer (0), buffer.getReadPointer (reader->numChannels > 1 ? 1 : 0) };
                blocks.process (ch, 2, n, out);

                for (double b : out)
                    stats.addBlock (b);
            }

            summary = "\n\nラウドネス: "_ju + juce::String (stats.integratedLufs(), 1) + " LUFS（目標 -14）\nピーク: "_ju
                      + juce::String (juce::Decibels::gainToDecibels (peak, -100.0f), 1) + " dBFS"_ju;
        }

        Dialogs::showInfo ("書き出し"_ju, file.getFullPathName() + "\nに書き出しました（48 kHz / 24 bit WAV）。"_ju + summary);
    });
}

void MainComponent::importAudio()
{
    chooser = std::make_unique<juce::FileChooser> ("オーディオを読み込む"_ju, juce::File(), AudioFiles::supportedWildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
    {
        auto files = fc.getResults();

        if (files.isEmpty())
            return;

        // 選択中のオーディオトラックの、再生位置（小節の頭）に置く
        const auto& map = document.getTempoMap();
        const auto tick = map.barToTick (map.tickToBar ((collab::Tick) state.playheadTick));
        std::string trackId;

        if (auto* t = ctx.selectedTrack(); t != nullptr && t->type == collab::TrackType::audio)
            trackId = t->id;

        ctx.importAudioFiles (files, trackId, tick);
    });
}

void MainComponent::showPluginManager()
{
    struct Manager  : public juce::Component
    {
        Manager (te::Engine& e, juce::PropertiesFile& props)
            : engine (e),
              list (e.getPluginManager().pluginFormatManager, e.getPluginManager().knownPluginList,
                    e.getTemporaryFileManager().getTempDirectory().getChildFile ("plugin-scan-dead-mans-pedal"), &props, true)
        {
            scanButton.setButtonText ("プラグインをスキャン"_ju);
            scanButton.onClick = [this]
            {
                PluginHost::ScanResult result;
                SyncUI::runWithProgress ("プラグインをスキャンしています（別プロセス）"_ju, [&]
                {
                    result = PluginHost::scan (engine, nullptr);
                    return juce::Result::ok();
                });

                juce::String text = juce::String (result.found) + " 個のプラグインがあります。"_ju;

                if (! result.blacklisted.isEmpty())
                    text << "\n" << "スキャン中に問題が起きたため、次のプラグインは読み込みません: "_ju
                         << result.blacklisted.joinIntoString ("、"_ju);

                info.setText (text, juce::dontSendNotification);
            };

            info.setText ("VST3（Windows / Mac）と AU（Mac）に対応しています。スキャンは別プロセスで行います。"_ju, juce::dontSendNotification);
            info.setColour (juce::Label::textColourId, Theme::textDim);
            addAndMakeVisible (scanButton);
            addAndMakeVisible (info);
            addAndMakeVisible (list);
            setSize (640, 480);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            auto top = r.removeFromTop (30);
            scanButton.setBounds (top.removeFromLeft (180));
            top.removeFromLeft (8);
            info.setBounds (top);
            r.removeFromTop (6);
            list.setBounds (r);
        }

        te::Engine& engine;
        juce::TextButton scanButton;
        juce::Label info;
        juce::PluginListComponent list;
    };

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new Manager (engine, settings));
    o.dialogTitle = "プラグイン"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::showAudioSettings()
{
    auto& dm = engine.getDeviceManager().deviceManager;

    // MIDI 入力は Tracktion が管理するので、JUCE の一覧ではなく下の「MIDI キーボード」欄で切り替える。
    // 入出力は機器のチャンネルをすべて使えるようにする（Babyface Pro FS など多チャンネルの機器。入力はモノラルごと、出力はステレオの組）
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (dm, 0, 256, 0, 256, false, false, false, false);
    auto note = std::make_unique<juce::Label>();
    note->setText ("サンプルレートは 48000 Hz 推奨。Windows で多チャンネルの機器は ASIO を選ぶ"_ju, juce::dontSendNotification);
    note->setFont (juce::FontOptions (15.0f));
    note->setColour (juce::Label::textColourId, Theme::textDim);

    selector->setBounds (0, 0, 560, 520);
    note->setBounds (8, 524, 544, 24);

    // マスター（曲の音）とメトロノームを出す出力（ステレオの組）
    struct MasterOut  : public juce::Component,
                        private juce::ChangeListener
    {
        explicit MasterOut (te::DeviceManager& d) : dm (d)
        {
            title.setText ("マスター出力"_ju, juce::dontSendNotification);
            title.setFont (juce::FontOptions (15.0f));
            addAndMakeVisible (title);
            box.onChange = [this]
            {
                const int i = box.getSelectedItemIndex();

                if (i >= 0 && i < ids.size() && ids[i] != dm.getDefaultWaveOutDeviceID())
                    dm.setDefaultWaveOutDevice (ids[i]);
            };
            addAndMakeVisible (box);
            dm.addChangeListener (this);
            refresh();
        }

        ~MasterOut() override   { dm.removeChangeListener (this); }

        void refresh()
        {
            box.clear (juce::dontSendNotification);
            ids.clear();

            for (int i = 0; i < dm.getNumWaveOutDevices(); ++i)
                if (auto* d = dm.getWaveOutDevice (i); d != nullptr && d->isEnabled())
                {
                    ids.add (d->getDeviceID());
                    box.addItem (d->getName(), ids.size());

                    if (d->getDeviceID() == dm.getDefaultWaveOutDeviceID())
                        box.setSelectedId (ids.size(), juce::dontSendNotification);
                }
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override    { refresh(); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 0);
            title.setBounds (r.removeFromLeft (150));
            box.setBounds (r.reduced (0, 2));
        }

        te::DeviceManager& dm;
        juce::Label title;
        juce::ComboBox box;
        juce::StringArray ids;
    };

    // レイテンシ補正（§3.5）: ドライバが報告する値で自動補正し、さらにデバイスごとに手動でずらせる
    struct Latency  : public juce::Component,
                      private juce::ChangeListener
    {
        Latency (MainComponent& o) : owner (o)
        {
            title.setText ("録音のレイテンシ補正（手動、サンプル）"_ju, juce::dontSendNotification);
            title.setFont (juce::FontOptions (14.5f));
            addAndMakeVisible (title);

            offset.setRange (-2000, 2000, 1);
            offset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
            offset.setDoubleClickReturnValue (true, 0);
            offset.onValueChange = [this]
            {
                if (auto key = owner.latencySettingKey(); key.isNotEmpty())
                    owner.settings.setValue (key, (int) offset.getValue());

                owner.applyLatencyOffset();
            };
            addAndMakeVisible (offset);

            info.setFont (juce::FontOptions (15.0f));
            info.setColour (juce::Label::textColourId, Theme::textDim);
            addAndMakeVisible (info);

            owner.engine.getDeviceManager().deviceManager.addChangeListener (this);
            update();
        }

        ~Latency() override
        {
            owner.engine.getDeviceManager().deviceManager.removeChangeListener (this);
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override    { update(); }

        void update()
        {
            auto* device = owner.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
            const auto key = owner.latencySettingKey();
            offset.setValue (key.isEmpty() ? 0 : owner.settings.getIntValue (key, 0), juce::dontSendNotification);
            offset.setEnabled (device != nullptr);

            if (device != nullptr)
                info.setText ("ドライバが報告するレイテンシ（自動で補正）: 入力 "_ju + juce::String (device->getInputLatencyInSamples())
                                + " / 出力 "_ju + juce::String (device->getOutputLatencyInSamples())
                                + " サンプル。録音がずれるときは、正の値で録音を前（早く）にずらします。"_ju,
                              juce::dontSendNotification);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (8, 0);
            auto row = area.removeFromTop (26);
            title.setBounds (row.removeFromLeft (260));
            offset.setBounds (row);
            info.setBounds (area);
        }

        MainComponent& owner;
        juce::Label title, info;
        juce::Slider offset { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    };

    auto latency = std::make_unique<Latency> (*this);
    auto masterOut = std::make_unique<MasterOut> (engine.getDeviceManager());
    masterOut->setBounds (0, 552, 560, 32);
    latency->setBounds (0, 590, 560, 64);

    struct Holder : juce::Component
    {
        std::unique_ptr<juce::Component> a, b, c, d, e;
    };

    auto holder = std::make_unique<Holder>();
    holder->a = std::move (selector);
    holder->b = std::move (note);
    holder->c = std::move (latency);
    holder->e = std::move (masterOut);
    holder->addAndMakeVisible (*holder->a);
    holder->addAndMakeVisible (*holder->b);
    holder->addAndMakeVisible (*holder->c);
    holder->addAndMakeVisible (*holder->e);

    const int midiHeight = 60 + juce::jmax (1, (int) bridge.getMidiInputs().size()) * 26 + 26;
    holder->d = std::make_unique<MidiInputPanel> (bridge);
    holder->d->setBounds (0, 660, 560, midiHeight);
    holder->addAndMakeVisible (*holder->d);
    holder->setSize (560, 660 + midiHeight);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (holder.release());
    o.dialogTitle = "オーディオ・MIDI の設定"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

//==============================================================================
void MainComponent::toggleRecord()
{
    if (bridge.isRecording())
    {
        bridge.stop();
        return;
    }

    std::vector<std::string> armed;

    for (auto& t : document.getProject().tracks)
        if (auto in = bridge.getTrackInput (t.id); in.armed && in.device.isNotEmpty())
            armed.push_back (t.id);

    // MIDI キーボードがつながっていれば、選択中の MIDI トラックにも録音する
    const auto midiInputs = bridge.getMidiInputs();
    const bool midiAvailable = std::any_of (midiInputs.begin(), midiInputs.end(), [] (auto& m) { return m.enabled; });
    auto* selected = midiRecordTarget();
    const bool midiTarget = midiAvailable && selected != nullptr;

    if (armed.empty() && ! midiTarget)
        return Dialogs::showInfo ("録音"_ju, "オーディオ: 録音するオーディオトラックの録音待機ボタン（●）をオンにしてください。\n"_ju
                                             "MIDI: MIDI キーボードをつないで（設定 → オーディオ・MIDI の設定で有効に）、"_ju
                                             "録音する MIDI トラックの録音待機ボタン（●）をオンにしてください。"_ju);

    if (! armed.empty() && ! document.hasLocation())
        return Dialogs::showInfo ("録音"_ju, "録音した音はプロジェクトのフォルダに保存するので、先にプロジェクトを保存してください。"_ju);

    if (midiTarget)
        armed.push_back (selected->id);

    if (auto r = bridge.startRecording (state.countInBars); r.failed())
        return Dialogs::showError ("録音できません"_ju, r.getErrorMessage());

    setStatus ("録音中（停止で確定）"_ju);
    commandManager.commandStatusChanged();
}

const collab::Track* MainComponent::midiRecordTarget() const
{
    const auto& p = document.getProject();

    if (auto* armed = p.findTrack (state.midiArmedTrackId); armed != nullptr && armed->type == collab::TrackType::midi)
        return armed;

    auto* t = p.findTrack (state.selectedTrackId);
    return t != nullptr && t->type == collab::TrackType::midi ? t : nullptr;
}

void MainComponent::importMidiRecording (std::vector<EngineBridge::RecordedMidi> recs)
{
    // 録音した範囲（小節単位）を 1 つの MIDI クリップにする
    const auto& map = document.getTempoMap();
    std::string lastTrack, lastClip;

    for (auto& rec : recs)
    {
        collab::Tick first = rec.punchInTick, last = rec.punchInTick + 1;

        for (auto& n : rec.notes)
        {
            first = std::min (first, n.tick);
            last = std::max (last, n.tick + n.lengthTick);
        }

        collab::MidiClip clip;
        clip.id = collab::generateUuid();
        clip.startTick = map.barToTick (map.tickToBar (first));
        clip.lengthTick = std::max<collab::Tick> (collab::kPpq, map.barToTick (map.tickToBar (last - 1) + 1) - clip.startTick);

        for (auto n : rec.notes)
        {
            n.tick -= clip.startTick;
            clip.notes.push_back (n);
        }

        const auto trackId = rec.trackId;
        document.perform ("MIDI の録音"_ju, [trackId, clip] (collab::Project& p)
        {
            if (auto* t = p.findTrack (trackId))
                t->midiClips.push_back (clip);
        });

        lastTrack = trackId;
        lastClip = clip.id;
        setStatus ("MIDI を録音しました（ノート "_ju + juce::String ((int) clip.notes.size()) + " 個）"_ju);
    }

    if (! lastClip.empty())
    {
        state.selectedTrackId = lastTrack;
        state.selectClip (lastClip);
        state.changed();
    }
}

void MainComponent::importTakes (std::vector<EngineBridge::RecordedTake> takes)
{
    std::vector<Takes::Clip> clips;
    const auto dir = document.getProjectDir();
    const auto map = document.getTempoMap();

    auto r = SyncUI::runWithProgress ("録音を保存しています"_ju, [&] { return Takes::import (takes, dir, map, clips); });

    Takes::addToProject (document, clips);
    commandManager.commandStatusChanged();

    if (r.failed())
        Dialogs::showError ("録音の一部を保存できませんでした"_ju, r.getErrorMessage());
    else
        setStatus ("録音しました（"_ju + juce::String ((int) clips.size()) + " テイク）"_ju);
}

juce::String MainComponent::latencySettingKey() const
{
    if (auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        return "latencyOffset_" + device->getTypeName() + "_" + device->getName();

    return {};
}

void MainComponent::applyLatencyOffset()
{
    const auto key = latencySettingKey();
    bridge.setManualLatencySamples (key.isEmpty() ? 0 : settings.getIntValue (key, 0));
}

void MainComponent::showCredits()
{
    juce::String text = "ShareDAW はオープンソースの JUCE / Tracktion Engine（GPL）、sfizz（BSD-2-Clause）を使用しています。\n\n"_ju
                        "内蔵音源:\n"_ju;

    for (auto* m : library.getAll())
    {
        text << "- " << toJuce (m->displayName) << " (" << toJuce (m->id) << " " << toJuce (m->version) << ")\n";

        for (auto& c : m->credits)
            text << "    " << toJuce (c) << "\n";
    }

    Dialogs::showInfo ("クレジット"_ju, text);
}

//==============================================================================
void MainComponent::deleteSelection()
{
    if (timeline.deleteLaneSelection())
        return;

    if (pianoRoll.hasSelectedNotes() && ! timeline.hasKeyboardFocus (true))
    {
        pianoRoll.deleteSelectedNotes();
        return;
    }

    ctx.deleteClips (state.clipSelection());
}

void MainComponent::duplicateClip()
{
    // ピアノロールでノートを選んでいればノート、なければ選択中のクリップ（複数可）
    if (pianoRoll.hasKeyboardFocus (true) && pianoRoll.hasSelectedNotes())
        return pianoRoll.duplicateSelectedNotes();

    if (state.clipSelection().size() != 1 || ctx.selectedClip() == nullptr)
        return ctx.duplicateClips (state.clipSelection());

    auto* clip = ctx.selectedClip();

    auto trackId = state.selectedTrackId;
    auto copy = *clip;
    copy.id = collab::generateUuid();
    copy.startTick = clip->endTick();

    for (auto& n : copy.notes)
        n.id = collab::generateUuid();

    document.perform ("クリップの複製"_ju, [trackId, copy] (collab::Project& p)
    {
        if (auto* t = p.findTrack (trackId))
            t->midiClips.push_back (copy);
    });

    state.selectClip (copy.id);
    state.changed();
}

//==============================================================================
juce::PopupMenu MainComponent::addTrackMenu()
{
    juce::PopupMenu m, instruments;
    instruments.addCommandItem (&commandManager, cmdAddDrums);
    instruments.addCommandItem (&commandManager, cmdAddBass);
    instruments.addCommandItem (&commandManager, cmdAddPiano);
    instruments.addCommandItem (&commandManager, cmdAddEPiano);

    m.addCommandItem (&commandManager, cmdAddAudioTrack);
    m.addSubMenu ("音源トラックを追加"_ju, instruments);
    m.addItem ("バストラックを追加"_ju, [this] { ctx.addBusTrack ("Bus"_ju); });
    return m;
}

void MainComponent::setOperationMode (OperationMode mode)
{
    state.mode = mode;
    settings.setValue ("operationMode", mode == OperationMode::studioOne ? "studioOne" : "cubase");

    // キー割り当てはモードごとの既定値から作り直す
    commandManager.getKeyMappings()->resetToDefaultMappings();
    commandManager.commandStatusChanged();
    state.changed();
}

void MainComponent::toggleMixer()
{
    if (mixerWindow == nullptr)
    {
        struct Window  : public juce::DocumentWindow
        {
            Window (MainComponent& o)
                : DocumentWindow ("ミキサー"_ju, Theme::panel, DocumentWindow::closeButton), owner (o) {}

            void closeButtonPressed() override
            {
                setVisible (false);
                owner.commandManager.commandStatusChanged();
            }

            MainComponent& owner;
        };

        auto window = std::make_unique<Window> (*this);
        window->setUsingNativeTitleBar (true);
        window->setContentOwned (new MixerView (ctx), true);
        window->setResizable (true, false);
        window->setResizeLimits (300, 280, 4000, 2000);
        window->addKeyListener (commandManager.getKeyMappings());   // ミキサーの上でも F3 などが効くように

        // 最初は画面いっぱい（メイン画面のあるディスプレイの作業領域）
        if (auto* top = getTopLevelComponent())
            if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (top->getScreenBounds()))
                window->setBounds (display->userArea);

        mixerWindow = std::move (window);
    }

    mixerWindow->setVisible (! mixerWindow->isVisible());

    if (mixerWindow->isVisible())
        mixerWindow->toFront (true);

    commandManager.commandStatusChanged();
}

void MainComponent::openChannelStrip (const std::string& trackId, bool compressor)
{
    if (document.getProject().findTrack (trackId) == nullptr)
        return;

    // EQ と Compressor で 1 つずつウィンドウを使い回し、開くトラックを切り替える
    auto& slot = compressor ? compWindow : eqWindow;

    if (slot == nullptr)
    {
        struct Window  : public juce::DocumentWindow
        {
            Window() : DocumentWindow ("EQ", Theme::panel, DocumentWindow::closeButton) {}
            void closeButtonPressed() override      { setVisible (false); }
        };

        auto window = std::make_unique<Window>();
        window->setUsingNativeTitleBar (true);
        auto* editor = new ChannelStripEditor (ctx, trackId, compressor ? ChannelStripEditor::Section::comp : ChannelStripEditor::Section::eq);
        auto* w = window.get();
        editor->onTitleChanged = [w, editor] { w->setName (editor->getTitle()); };
        window->setContentOwned (editor, true);
        window->setResizable (false, false);
        window->addKeyListener (commandManager.getKeyMappings());

        if (auto* top = getTopLevelComponent())
            window->setTopLeftPosition (top->getX() + (compressor ? 160 : 120), top->getY() + (compressor ? 160 : 120));

        slot = std::move (window);
    }

    if (auto* editor = dynamic_cast<ChannelStripEditor*> (slot->getContentComponent()))
    {
        editor->setTrack (trackId);
        slot->setName (editor->getTitle());
    }

    slot->setVisible (true);
    slot->toFront (true);
}

void MainComponent::openMaster()
{
    if (masterWindow == nullptr)
    {
        struct Window  : public juce::DocumentWindow
        {
            Window (MainComponent& o) : DocumentWindow ("マスター - Vintage Limiter / Loudness"_ju, Theme::panel, DocumentWindow::closeButton), owner (o) {}

            void closeButtonPressed() override
            {
                setVisible (false);
                owner.commandManager.commandStatusChanged();
            }

            MainComponent& owner;
        };

        auto window = std::make_unique<Window> (*this);
        window->setUsingNativeTitleBar (true);
        window->setContentOwned (new MasterPanel (ctx), true);
        window->setResizable (false, false);
        window->addKeyListener (commandManager.getKeyMappings());

        if (auto* top = getTopLevelComponent())
            window->setTopLeftPosition (top->getX() + 160, top->getY() + 140);

        masterWindow = std::move (window);
    }

    // F4 で開く・閉じる
    const bool show = ! masterWindow->isVisible() || ! masterWindow->isActiveWindow();
    masterWindow->setVisible (show);

    if (show)
        masterWindow->toFront (true);

    commandManager.commandStatusChanged();
}

void MainComponent::showShortcuts()
{
    const juce::String text (
        "■ ツール（Cubase と同じ番号。テンキーでも可）\n"_ju
        "  1 選択 / 2 鉛筆 / 3 はさみ\n"_ju
        "  空いている所を右クリックでツールの切り替え・貼り付け・トラックの追加\n"_ju
        "\n"_ju
        "■ クリップ（タイムライン）\n"_ju
        "  クリック: 選択　Ctrl/Shift+クリック: 選択に追加・解除　空いている所をドラッグ: 範囲選択\n"_ju
        "  ドラッグ: 移動（複数でもまとめて）　左端・右端をドラッグ: 長さ（MIDI・オーディオとも）\n"_ju
        "  Alt を押しながら: スナップを一時的に解除\n"_ju
        "  Ctrl+C / X / V: コピー / 切り取り / 貼り付け（再生位置へ）　Ctrl+D: 複製　Delete: 削除\n"_ju
        "  Ctrl+← / →: クオンタイズ値ずつずらす　Alt+X: 再生位置で分割\n"_ju
        "  M / S: 選択中のトラックのミュート / ソロ\n"_ju
        "  鉛筆: 空いている所をクリック（ドラッグで長さ）で MIDI クリップを作成　ダブルクリック: ピアノロールで開く\n"_ju
        "\n"_ju
        "■ ピアノロール\n"_ju
        "  鉛筆: クリックで追加（ドラッグで長さ）、ノートをクリックで削除\n"_ju
        "  選択: ドラッグで移動・範囲選択、右端で長さ　↑↓: 半音　Shift/Ctrl+↑↓: オクターブ　←→: クオンタイズ値ずつ\n"_ju
        "  はさみ: クリック位置でノートを分割\n"_ju
        "  Ctrl+C / X / V / D: コピー / 切り取り / 貼り付け / 複製　Q: クオンタイズ　Ctrl+A: すべて選択\n"_ju
        "\n"_ju
        "■ 再生・録音\n"_ju
        "  Space: 再生／停止　テンキー 0: 停止（停止中なら先頭へ）　Home / テンキー .: 先頭へ\n"_ju
        "  テンキー + / -: 1 小節進む / 戻る　R / テンキー *: 録音　L / テンキー /: ループ　P: 選択範囲をループ範囲に\n"_ju
        "  C: メトロノーム　F: 自動スクロール　J: スナップ　G / H: 縮小 / 拡大\n"_ju
        "\n"_ju
        "■ マーカー\n"_ju
        "  Insert: 再生位置に追加　Shift+1〜9: マーカーへ移動　ダブルクリック: 名前\n"_ju
        "\n"_ju
        "■ そのほか\n"_ju
        "  F3: ミキサー　F4: マスター（リミッター / ラウドネス）　F7: 同期パネル　Ctrl+Z / Ctrl+Shift+Z: 元に戻す / やり直し　Ctrl+S: 保存　Ctrl+I: オーディオを読み込む\n"_ju
        "  BPM・拍子: トランスポートバーの数字をクリックして入力、ホイールで増減\n"_ju);

    auto editor = std::make_unique<juce::TextEditor>();
    editor->setMultiLine (true);
    editor->setReadOnly (true);
    editor->setScrollbarsShown (true);
    editor->setFont (juce::FontOptions (15.5f));
    editor->setColour (juce::TextEditor::backgroundColourId, Theme::background);
    editor->setColour (juce::TextEditor::textColourId, Theme::text);
    editor->setText (text, false);
    editor->setSize (760, 560);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (editor.release());
    o.dialogTitle = "操作とショートカット"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::zoom (double factor)
{
    // ピアノロールにフォーカスがあればピアノロール、それ以外はタイムライン。再生位置を中心に拡大・縮小する
    const bool piano = pianoRoll.hasKeyboardFocus (true);
    auto& axis = piano ? state.pianoRoll : state.timeline;
    const double x = axis.tickToX (bridge.getPositionTick());
    const double width = piano ? pianoRoll.getWidth() : timeline.getWidth() - TimelineView::headerWidth;
    axis.zoomAround (juce::jlimit (0.0, juce::jmax (0.0, width), x), factor);
    state.changed();
}

void MainComponent::loopToSelection()
{
    // ピアノロールで選んだノート、なければタイムラインで選んだクリップの範囲
    collab::Tick start = -1, end = -1;
    const auto& map = document.getTempoMap();

    if (auto* clip = ctx.selectedClip(); clip != nullptr && pianoRoll.hasSelectedNotes())
    {
        for (auto& n : clip->notes)
            if (pianoRoll.selectedNotes.count (n.id) > 0)
            {
                start = start < 0 ? clip->startTick + n.tick : juce::jmin (start, clip->startTick + n.tick);
                end = juce::jmax (end, clip->startTick + n.endTick());
            }
    }
    else if (auto* track = ctx.selectedTrack(); track != nullptr && ! state.selectedClipId.empty())
    {
        if (auto* c = track->findMidiClip (state.selectedClipId))
        {
            start = c->startTick;
            end = c->endTick();
        }

        for (auto& c : track->audioClips)
            if (c.id == state.selectedClipId)
            {
                start = c.startTick;
                end = collab::audioClipEndTick (c, map);
            }
    }

    if (start < 0 || end <= start)
        return Dialogs::showInfo ("ループ範囲"_ju, "ループ範囲にするクリップ（またはノート）を選択してください"_ju);

    state.loopStart = start;
    state.loopEnd = end;
    state.loopEnabled = true;
    state.changed();
    setStatus ("ループ範囲: "_ju + juce::String (map.tickToBar (start)) + " 小節目から "_ju
               + juce::String (map.tickToBar (juce::jmax<collab::Tick> (start, end - 1))) + " 小節目まで"_ju);
}

//==============================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.addArray ({ cmdNew, cmdOpen, cmdSave, cmdUndo, cmdRedo, cmdDelete, cmdSelectAll, cmdDuplicate,
                         cmdPlay, cmdToStart, cmdLoop, cmdMetronome, cmdQuantise,
                         cmdAddDrums, cmdAddBass, cmdAddPiano, cmdAddEPiano, cmdAudioSettings, cmdCredits, cmdAbout, cmdCheckUpdate,
                         cmdRecord, cmdCountIn0, cmdCountIn1, cmdCountIn2,
                         cmdFont100, cmdFont125, cmdFont150, cmdFont175, cmdFont200,
                         cmdSyncSettings, cmdSyncRegister, cmdSyncOpen, cmdSyncPull, cmdSyncPush, cmdSyncHistory,
                         cmdAddAudioTrack, cmdImportAudio, cmdImportMidi, cmdExportMixdown, cmdSplit, cmdMuteTrack, cmdSoloTrack, cmdPlugins, cmdArmTrack, cmdTrackHeight,
                         cmdToolSelect, cmdToolPencil, cmdModeCubase, cmdModeStudioOne, cmdMixer, cmdMaster, cmdLoopToSelection,
                         cmdStop, cmdZoomIn, cmdZoomOut, cmdSnap, cmdAutoScroll, cmdAddMarker,
                         cmdMarker1, cmdMarker2, cmdMarker3, cmdMarker4, cmdMarker5, cmdMarker6, cmdMarker7, cmdMarker8, cmdMarker9,
                         cmdToolSplit, cmdCopy, cmdCut, cmdPaste, cmdNudgeLeft, cmdNudgeRight,
                         cmdForward, cmdRewind, cmdShortcuts, cmdSyncPanel, cmdSyncCreate, cmdToLoopStart, cmdToLoopEnd, cmdInspector });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using KP = juce::KeyPress;
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const float currentScale = juce::Desktop::getInstance().getGlobalScaleFactor();

    switch (id)
    {
        case cmdNew:        info.setInfo ("新しい曲（サーバーに作る）…"_ju, {}, "File", 0); info.addDefaultKeypress ('n', cmd); break;
        case cmdOpen:       info.setInfo ("楽曲を開く…"_ju, {}, "File", 0); info.addDefaultKeypress ('o', cmd); break;
        case cmdSave:       info.setInfo ("保存"_ju, {}, "File", 0); info.addDefaultKeypress ('s', cmd); break;
        case cmdUndo:
            info.setInfo ("元に戻す "_ju + document.getUndoDescription(), {}, "Edit", 0);
            info.addDefaultKeypress ('z', cmd);
            info.setActive (document.canUndo());
            break;
        case cmdRedo:
            info.setInfo ("やり直し "_ju + document.getRedoDescription(), {}, "Edit", 0);
            info.addDefaultKeypress ('z', cmd | shift);
            info.addDefaultKeypress ('y', cmd);
            info.setActive (document.canRedo());
            break;
        case cmdDelete:
            info.setInfo ("削除"_ju, {}, "Edit", 0);
            info.addDefaultKeypress (KP::deleteKey, 0);
            info.addDefaultKeypress (KP::backspaceKey, 0);
            break;
        case cmdSelectAll:  info.setInfo ("すべてのノートを選択"_ju, {}, "Edit", 0); info.addDefaultKeypress ('a', cmd); break;
        case cmdDuplicate:  info.setInfo ("複製（クリップ・ノート）"_ju, {}, "Edit", 0); info.addDefaultKeypress ('d', cmd); break;
        case cmdCopy:       info.setInfo ("コピー"_ju, {}, "Edit", 0); info.addDefaultKeypress ('c', cmd); break;
        case cmdCut:        info.setInfo ("切り取り"_ju, {}, "Edit", 0); info.addDefaultKeypress ('x', cmd); break;
        case cmdPaste:      info.setInfo ("貼り付け（再生位置へ）"_ju, {}, "Edit", 0); info.addDefaultKeypress ('v', cmd); break;
        case cmdNudgeLeft:  info.setInfo ("クリップを左へずらす"_ju, {}, "Edit", 0); info.addDefaultKeypress (KP::leftKey, cmd); break;
        case cmdNudgeRight: info.setInfo ("クリップを右へずらす"_ju, {}, "Edit", 0); info.addDefaultKeypress (KP::rightKey, cmd); break;
        case cmdForward:    info.setInfo ("1 小節進む"_ju, {}, "Transport", 0); info.addDefaultKeypress (KP::numberPadAdd, 0); break;
        case cmdRewind:     info.setInfo ("1 小節戻る"_ju, {}, "Transport", 0); info.addDefaultKeypress (KP::numberPadSubtract, 0); break;
        case cmdShortcuts:  info.setInfo ("操作とショートカットの一覧…"_ju, {}, "Help", 0); info.addDefaultKeypress (KP::F1Key, 0); break;
        case cmdToLoopStart: info.setInfo ("左ロケーターへ移動"_ju, {}, "Transport", 0); info.addDefaultKeypress (KP::numberPad1, 0); break;
        case cmdToLoopEnd:   info.setInfo ("右ロケーターへ移動"_ju, {}, "Transport", 0); info.addDefaultKeypress (KP::numberPad2, 0); break;
        case cmdToolSplit:
            info.setInfo ("はさみツール"_ju, {}, "Edit", 0);
            info.defaultKeypresses.add (state.behaviour().splitToolKey);
            info.setTicked (state.tool == EditTool::split);
            break;
        case cmdQuantise:   info.setInfo ("クオンタイズ"_ju, {}, "Edit", 0); info.addDefaultKeypress ('q', 0); break;
        case cmdPlay:       info.setInfo ("再生／停止"_ju, {}, "Transport", 0); info.addDefaultKeypress (KP::spaceKey, 0); break;
        case cmdToStart:
            info.setInfo ("先頭へ"_ju, {}, "Transport", 0);
            info.addDefaultKeypress (KP::homeKey, 0);
            info.defaultKeypresses.add (state.behaviour().toStartKey);
            break;
        case cmdStop:
            info.setInfo ("停止（停止中なら先頭へ）"_ju, {}, "Transport", 0);
            info.defaultKeypresses.add (state.behaviour().stopKey);
            break;
        case cmdZoomIn:
            info.setInfo ("拡大（横）"_ju, {}, "View", 0);
            info.defaultKeypresses.add (state.behaviour().zoomInKey);
            break;
        case cmdZoomOut:
            info.setInfo ("縮小（横）"_ju, {}, "View", 0);
            info.defaultKeypresses.add (state.behaviour().zoomOutKey);
            break;
        case cmdAddMarker:
            info.setInfo ("再生位置にマーカーを追加"_ju, {}, "Transport", 0);
            info.addDefaultKeypress (KP::insertKey, 0);
            break;
        case cmdMarker1: case cmdMarker2: case cmdMarker3: case cmdMarker4: case cmdMarker5:
        case cmdMarker6: case cmdMarker7: case cmdMarker8: case cmdMarker9:
        {
            const int n = (int) id - cmdMarker1 + 1;
            const auto markers = MarkerLane::sorted (document.getProject());
            auto name = "マーカー "_ju + juce::String (n) + " へ移動"_ju;

            if (n <= (int) markers.size() && ! markers[(size_t) n - 1].name.empty())
                name << "（"_ju << toJuce (markers[(size_t) n - 1].name) << "）"_ju;

            info.setInfo (name, {}, "Transport", 0);
            info.addDefaultKeypress ('0' + n, shift);

            // OS やキー配列によっては Shift + 数字が記号として届くので、US / JIS 配列の記号でも受ける
            for (auto* symbols : { "!@#$%^&*(", "!\"#$%&'()" })
                if (const auto c = (juce::juce_wchar) (unsigned char) symbols[n - 1]; c != (juce::juce_wchar) ('0' + n))
                    info.defaultKeypresses.addIfNotAlreadyThere (KP (c, shift, 0));
            info.setActive (n <= (int) markers.size());
            break;
        }
        case cmdSnap:
            info.setInfo ("スナップ（クオンタイズ値に合わせる）"_ju, {}, "Edit", 0);
            info.defaultKeypresses.add (state.behaviour().snapKey);
            info.setTicked (state.snapEnabled());
            break;
        case cmdAutoScroll:
            info.setInfo ("自動スクロール"_ju, {}, "View", 0);
            info.defaultKeypresses.add (state.behaviour().autoScrollKey);
            info.setTicked (state.autoScroll);
            break;
        case cmdRecord:
            info.setInfo ("録音"_ju, {}, "Transport", 0);
            info.defaultKeypresses.add (state.behaviour().recordKey);
            info.addDefaultKeypress ('*', 0);                              // キーボードの * も（配列によって Shift が付く）
            info.addDefaultKeypress ('*', shift);
            info.setTicked (bridge.isRecording());
            break;
        case cmdCountIn0:
        case cmdCountIn1:
        case cmdCountIn2:
        {
            const int bars = id - cmdCountIn0;
            info.setInfo (bars == 0 ? "カウントインなし"_ju : "カウントイン "_ju + juce::String (bars) + " 小節"_ju, {}, "Transport", 0);
            info.setTicked (state.countInBars == bars);
            break;
        }
        case cmdLoop:
            info.setInfo ("ループ"_ju, {}, "Transport", 0);
            info.addDefaultKeypress ('l', 0);
            info.defaultKeypresses.add (state.behaviour().loopKey);
            info.setTicked (state.loopEnabled);
            break;
        case cmdMetronome:
            info.setInfo ("メトロノーム"_ju, {}, "Transport", 0);
            info.addDefaultKeypress ('c', 0);
            info.setTicked (state.metronomeEnabled);
            break;
        case cmdAddDrums:   info.setInfo ("ドラム"_ju, {}, "Track", 0); break;
        case cmdAddBass:    info.setInfo ("ベース"_ju, {}, "Track", 0); break;
        case cmdAddPiano:   info.setInfo ("ピアノ"_ju, {}, "Track", 0); break;
        case cmdAddEPiano:  info.setInfo ("エレピ"_ju, {}, "Track", 0); break;
        case cmdToolSelect:
            info.setInfo ("選択ツール"_ju, {}, "Edit", 0);
            info.defaultKeypresses.add (state.behaviour().selectToolKey);
            info.setTicked (state.tool == EditTool::select);
            break;
        case cmdToolPencil:
            info.setInfo ("鉛筆ツール"_ju, {}, "Edit", 0);
            info.defaultKeypresses.add (state.behaviour().pencilToolKey);
            info.setTicked (state.tool == EditTool::pencil);
            break;
        case cmdModeCubase:
            info.setInfo ("Cubase モード"_ju, {}, "View", 0);
            info.setTicked (state.mode == OperationMode::cubase);
            break;
        case cmdModeStudioOne:
            info.setInfo ("Studio One モード（いまは Cubase と同じ操作）"_ju, {}, "View", 0);
            info.setTicked (state.mode == OperationMode::studioOne);
            break;
        case cmdMixer:
            info.setInfo ("ミキサー"_ju, {}, "View", 0);
            info.defaultKeypresses.add (state.behaviour().mixerKey);
            info.setTicked (mixerWindow != nullptr && mixerWindow->isVisible());
            break;
        case cmdMaster:
            info.setInfo ("マスター（リミッター / ラウドネス）"_ju, {}, "View", 0);
            info.defaultKeypresses.add (juce::KeyPress (juce::KeyPress::F4Key));
            info.setTicked (masterWindow != nullptr && masterWindow->isVisible());
            break;
        case cmdLoopToSelection:
            info.setInfo ("ループ範囲を選択範囲に合わせる"_ju, {}, "Transport", 0);
            info.defaultKeypresses.add (state.behaviour().loopToSelectionKey);
            break;
        case cmdPlugins:       info.setInfo ("プラグイン（スキャン・一覧）…"_ju, {}, "Options", 0); break;
        case cmdAddAudioTrack: info.setInfo ("オーディオトラックを追加"_ju, {}, "Track", 0); break;
        case cmdImportAudio:   info.setInfo ("オーディオを読み込む…"_ju, {}, "File", 0); info.addDefaultKeypress ('i', cmd); break;
        case cmdImportMidi:    info.setInfo ("MIDI ファイルを読み込む…"_ju, {}, "File", 0); break;
        case cmdExportMixdown:
            info.setInfo ("ミックスダウンを書き出す（WAV）…"_ju, {}, "File", 0);
            break;
        case cmdSplit:         info.setInfo ("再生位置で分割"_ju, {}, "Edit", 0); info.addDefaultKeypress ('x', juce::ModifierKeys::altModifier); break;
        case cmdMuteTrack:
            info.setInfo ("選択中のトラックのミュート"_ju, {}, "Track", 0);
            info.addDefaultKeypress ('m', 0);
            info.setActive (ctx.selectedTrack() != nullptr);
            break;
        case cmdSoloTrack:
            info.setInfo ("選択中のトラックのソロ"_ju, {}, "Track", 0);
            info.addDefaultKeypress ('s', 0);
            info.setActive (ctx.selectedTrack() != nullptr);
            break;
        case cmdArmTrack:
            info.setInfo ("選択中のトラックの録音待機"_ju, {}, "Track", 0);
            info.addDefaultKeypress ('r', 0);
            info.setActive (ctx.selectedTrack() != nullptr);
            break;
        case cmdTrackHeight:
            info.setInfo ("選択中のトラックの高さ（最大 / 最小）"_ju, {}, "Track", 0);
            info.addDefaultKeypress ('z', 0);
            info.setActive (ctx.selectedTrack() != nullptr);
            break;
        case cmdAudioSettings: info.setInfo ("オーディオ・MIDI の設定…"_ju, {}, "Options", 0); break;
        case cmdSyncSettings:  info.setInfo ("サーバー設定…"_ju, {}, "Sync", 0); break;
        case cmdSyncRegister:  info.setInfo ("このプロジェクトをサーバーに登録…"_ju, {}, "Sync", 0); info.setActive (! sync.isLinked()); break;
        case cmdSyncOpen:
            info.setInfo ("楽曲を選ぶ（サーバー / この PC）…"_ju, {}, "File", 0);
            info.addDefaultKeypress ('o', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case cmdSyncPull:      info.setInfo ("ダウンロード（サーバーの新しい変更を取り込む）"_ju, {}, "Sync", 0); info.setActive (sync.isLinked()); break;
        case cmdSyncPush:      info.setInfo ("アップ（同期パネルを開く）"_ju, {}, "Sync", 0); info.setActive (sync.isLinked()); break;
        case cmdSyncHistory:   info.setInfo ("リビジョン履歴…"_ju, {}, "Sync", 0); info.setActive (sync.isLinked()); break;
        case cmdSyncPanel:
            info.setInfo ("同期パネル"_ju, {}, "Sync", 0);
            info.addDefaultKeypress (juce::KeyPress::F7Key, 0);
            info.setTicked (! syncPanel.isCollapsed());
            break;
        case cmdSyncCreate:    info.setInfo ("サーバーに新しい曲を作る…"_ju, {}, "Sync", 0); break;
        case cmdInspector:
            info.setInfo ("インスペクター"_ju, {}, "View", 0);
            info.addDefaultKeypress ('i', juce::ModifierKeys::altModifier);
            info.setTicked (inspector.isVisible());
            break;
        case cmdCredits:    info.setInfo ("クレジット…"_ju, {}, "Help", 0); break;
        case cmdAbout:      info.setInfo ("ShareDAW について…"_ju, {}, "Help", 0); break;
        case cmdCheckUpdate: info.setInfo ("アップデートを確認…"_ju, {}, "Help", 0); break;
        case cmdFont100: case cmdFont125: case cmdFont150: case cmdFont175: case cmdFont200:
        {
            const float scale = fontScales[id - cmdFont100];
            info.setInfo (juce::String (juce::roundToInt (scale * 100)) + "%", {}, "View", 0);
            info.setTicked (std::abs (currentScale - scale) < 0.01f);
            break;
        }
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case cmdNew:        createProjectOnServer(); break;
        case cmdOpen:       showProjectPicker(); break;
        case cmdSave:       saveProject(); break;
        case cmdUndo:       document.undo(); break;
        case cmdRedo:       document.redo(); break;
        case cmdDelete:     deleteSelection(); break;
        case cmdSelectAll:  pianoRoll.selectAllNotes(); break;
        case cmdDuplicate:  duplicateClip(); break;
        case cmdQuantise:   pianoRoll.quantiseSelection(); break;
        case cmdPlay:       bridge.togglePlay(); break;
        case cmdToStart:    bridge.returnToStart(); break;
        case cmdStop:
            if (bridge.isPlaying())
                bridge.stop();
            else
                bridge.returnToStart();
            break;
        case cmdZoomIn:     zoom (1.25); break;
        case cmdZoomOut:    zoom (0.8); break;
        case cmdAddMarker:
        {
            const auto t = (collab::Tick) std::llround (state.snapCursor (bridge.getPositionTick(), document.getTempoMap(), {}));
            MarkerLane::addMarker (ctx, t);
            break;
        }
        case cmdMarker1: case cmdMarker2: case cmdMarker3: case cmdMarker4: case cmdMarker5:
        case cmdMarker6: case cmdMarker7: case cmdMarker8: case cmdMarker9:
        {
            const auto markers = MarkerLane::sorted (document.getProject());
            const auto n = (size_t) (info.commandID - cmdMarker1);

            if (n < markers.size())
            {
                bridge.setPositionTick ((double) markers[n].tick);
                timeline.followPlayhead ((double) markers[n].tick);   // 画面もそこへ
                pianoRoll.followPlayhead ((double) markers[n].tick);
                state.selectedMarkerId = markers[n].id;
                state.changed();
            }
            break;
        }
        case cmdSnap:
            state.setSnapEnabled (! state.snapEnabled());
            break;
        case cmdAutoScroll:
            state.autoScroll = ! state.autoScroll;
            state.changed();
            break;
        case cmdRecord:     toggleRecord(); break;
        case cmdCountIn0:
        case cmdCountIn1:
        case cmdCountIn2:
            state.countInBars = info.commandID - cmdCountIn0;
            settings.setValue ("countInBars", state.countInBars);
            state.changed();
            break;
        case cmdLoop:       state.loopEnabled = ! state.loopEnabled; state.changed(); break;
        case cmdMetronome:  state.metronomeEnabled = ! state.metronomeEnabled; state.changed(); break;
        case cmdAddDrums:   ctx.addBuiltinMidiTrack (collab::builtin::drums, "Drums"); break;
        case cmdAddBass:    ctx.addBuiltinMidiTrack (collab::builtin::bass, "Bass"); break;
        case cmdAddPiano:   ctx.addBuiltinMidiTrack (collab::builtin::piano, "Piano"); break;
        case cmdAddEPiano:  ctx.addBuiltinMidiTrack (collab::builtin::epiano, "E.Piano"); break;
        case cmdAddAudioTrack: ctx.addAudioTrack ("Audio"); break;
        case cmdToolSelect:    state.tool = EditTool::select; state.changed(); break;
        case cmdToolPencil:    state.tool = EditTool::pencil; state.changed(); break;
        case cmdToolSplit:     state.tool = EditTool::split; state.changed(); break;
        case cmdCopy:
        case cmdCut:
            if (pianoRoll.hasKeyboardFocus (true) && pianoRoll.hasSelectedNotes())
                pianoRoll.copySelectedNotes (info.commandID == cmdCut);
            else if (timeline.copyRange (info.commandID == cmdCut))
                lastCopiedRange = true;
            else if (timeline.copyChord (info.commandID == cmdCut))
                lastCopiedRange = false;
            else
            {
                ctx.copyClips (state.clipSelection());

                if (info.commandID == cmdCut)
                    ctx.deleteClips (state.clipSelection());
            }
            break;
        case cmdPaste:
            if (pianoRoll.hasKeyboardFocus (true) && pianoRoll.hasNotesInClipboard())
                pianoRoll.pasteNotes();
            else if (lastCopiedRange && timeline.pasteRange (bridge.getPositionTick()))
                break;
            else if (timeline.pasteChord (bridge.getPositionTick()))
                break;
            else
                ctx.pasteClips ((collab::Tick) std::llround (state.snapCursor (bridge.getPositionTick(), document.getTempoMap(), {})));
            break;
        case cmdNudgeLeft:
        case cmdNudgeRight:
        {
            const auto step = std::max<collab::Tick> (1, state.grid.stepTicks());
            ctx.nudgeClips (state.clipSelection(), info.commandID == cmdNudgeLeft ? -step : step);
            break;
        }
        case cmdToLoopStart:   bridge.setPositionTick ((double) state.loopStart); break;
        case cmdToLoopEnd:     bridge.setPositionTick ((double) state.loopEnd); break;
        case cmdForward:
        case cmdRewind:
        {
            const auto& map = document.getTempoMap();
            const int bar = map.tickToBar ((collab::Tick) std::llround (bridge.getPositionTick()));
            const auto barStart = map.barToTick (bar);
            const bool onBar = std::llabs ((collab::Tick) std::llround (bridge.getPositionTick()) - barStart) < 5;
            const int target = info.commandID == cmdForward ? bar + 1 : (onBar ? juce::jmax (1, bar - 1) : bar);
            bridge.setPositionTick ((double) map.barToTick (target));
            break;
        }
        case cmdShortcuts:     showShortcuts(); break;
        case cmdModeCubase:    setOperationMode (OperationMode::cubase); break;
        case cmdModeStudioOne: setOperationMode (OperationMode::studioOne); break;
        case cmdMixer:         toggleMixer(); break;
        case cmdMaster:        openMaster(); break;
        case cmdLoopToSelection: loopToSelection(); break;
        case cmdPlugins:       showPluginManager(); break;
        case cmdImportAudio:   importAudio(); break;
        case cmdImportMidi:    importMidi(); break;
        case cmdExportMixdown: exportMixdown(); break;
        case cmdSplit:         ctx.splitAtPlayhead(); break;
        case cmdMuteTrack:
        case cmdSoloTrack:
            if (auto* t = ctx.selectedTrack())
            {
                const bool mute = info.commandID == cmdMuteTrack;
                auto id = t->id;
                document.perform (mute ? "ミュート"_ju : "ソロ"_ju, [id, mute] (collab::Project& p)
                {
                    if (auto* tr = p.findTrack (id))
                        (mute ? tr->mute : tr->solo) = ! (mute ? tr->mute : tr->solo);
                });
            }
            break;
        case cmdArmTrack:
            if (auto* t = ctx.selectedTrack())
                ctx.toggleRecordArm (t->id);
            break;
        case cmdTrackHeight:
            if (auto* t = ctx.selectedTrack())
            {
                const bool isMax = state.trackHeight (t->id) >= EditorState::maxTrackHeight;
                state.trackHeights[t->id] = isMax ? EditorState::minTrackHeight : EditorState::maxTrackHeight;
                state.changed();
            }
            break;
        case cmdAudioSettings: showAudioSettings(); break;
        case cmdSyncSettings:  showServerSettings(); break;
        case cmdSyncRegister:  registerProject(); break;
        case cmdSyncOpen:      showProjectPicker(); break;
        case cmdSyncPull:      downloadWithChoices ({}, false); break;
        case cmdSyncPush:      if (syncPanel.isCollapsed()) toggleSyncPanel(); break;
        case cmdSyncHistory:   showHistory(); break;
        case cmdSyncPanel:     toggleSyncPanel(); break;
        case cmdInspector:
            inspector.setVisible (! inspector.isVisible());
            settings.setValue ("inspectorVisible", inspector.isVisible());
            resized();
            commandManager.commandStatusChanged();
            break;
        case cmdSyncCreate:    createProjectOnServer(); break;
        case cmdCredits:    showCredits(); break;
        case cmdAbout:
            Dialogs::showInfo ("ShareDAW について"_ju,
                               "ShareDAW "_ju + juce::String (JUCE_APPLICATION_VERSION_STRING)
                                 + (Updater::currentBuild() > 0 ? " (build "_ju + juce::String (Updater::currentBuild()) + ")"
                                                                : "（手元でビルドした開発版）"_ju)
                                 + "\n共同制作用の軽量DAW（アイデア出し・ラフ録音用）"_ju);
            break;
        case cmdCheckUpdate: checkForUpdates (true); break;
        case cmdFont100: case cmdFont125: case cmdFont150: case cmdFont175: case cmdFont200:
        {
            const float scale = fontScales[info.commandID - cmdFont100];
            applyFontScale (scale);
            settings.setValue ("uiScale", scale);
            commandManager.commandStatusChanged();
            break;
        }
        default: return false;
    }

    return true;
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "ファイル"_ju, "編集"_ju, "トランスポート"_ju, "トラック"_ju, "同期"_ju, "表示"_ju, "設定"_ju, "ヘルプ"_ju };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    auto* cm = &commandManager;

    switch (index)
    {
        case 0:
            m.addCommandItem (cm, cmdNew);
            m.addCommandItem (cm, cmdSyncOpen);
            m.addCommandItem (cm, cmdOpen);
            m.addCommandItem (cm, cmdSave);
            m.addSeparator();
            m.addCommandItem (cm, cmdImportAudio);
            m.addCommandItem (cm, cmdImportMidi);
            m.addCommandItem (cm, cmdExportMixdown);
           #if ! JUCE_MAC
            m.addSeparator();
            m.addCommandItem (cm, juce::StandardApplicationCommandIDs::quit);
           #endif
            break;
        case 1:
            m.addCommandItem (cm, cmdUndo);
            m.addCommandItem (cm, cmdRedo);
            m.addSeparator();
            m.addCommandItem (cm, cmdCut);
            m.addCommandItem (cm, cmdCopy);
            m.addCommandItem (cm, cmdPaste);
            m.addCommandItem (cm, cmdDelete);
            m.addCommandItem (cm, cmdSelectAll);
            m.addCommandItem (cm, cmdDuplicate);
            m.addCommandItem (cm, cmdNudgeLeft);
            m.addCommandItem (cm, cmdNudgeRight);
            m.addCommandItem (cm, cmdSplit);
            m.addCommandItem (cm, cmdQuantise);
            m.addSeparator();
            m.addCommandItem (cm, cmdToolSelect);
            m.addCommandItem (cm, cmdToolPencil);
            m.addCommandItem (cm, cmdToolSplit);
            m.addCommandItem (cm, cmdSnap);
            break;
        case 2:
            m.addCommandItem (cm, cmdPlay);
            m.addCommandItem (cm, cmdStop);
            m.addCommandItem (cm, cmdRecord);
            m.addCommandItem (cm, cmdToStart);
            m.addCommandItem (cm, cmdToLoopStart);
            m.addCommandItem (cm, cmdToLoopEnd);
            m.addCommandItem (cm, cmdLoop);
            m.addCommandItem (cm, cmdLoopToSelection);
            m.addSeparator();
            m.addCommandItem (cm, cmdAddMarker);

            {
                juce::PopupMenu markers;

                for (int c = cmdMarker1; c <= cmdMarker9; ++c)
                    markers.addCommandItem (cm, c);

                m.addSubMenu ("マーカーへ移動"_ju, markers);
            }
            m.addCommandItem (cm, cmdMetronome);
            m.addSeparator();
            m.addCommandItem (cm, cmdCountIn0);
            m.addCommandItem (cm, cmdCountIn1);
            m.addCommandItem (cm, cmdCountIn2);
            break;
        case 3:
            m = addTrackMenu();
            break;
        case 4:
        {
            m.addCommandItem (cm, cmdSyncPanel);
            m.addSeparator();
            m.addCommandItem (cm, cmdSyncPull);
            m.addCommandItem (cm, cmdSyncPush);
            m.addSeparator();
            m.addCommandItem (cm, cmdSyncHistory);

            m.addSeparator();
            m.addCommandItem (cm, cmdSyncCreate);
            m.addCommandItem (cm, cmdSyncRegister);
            m.addCommandItem (cm, cmdSyncOpen);
            m.addCommandItem (cm, cmdSyncSettings);
            break;
        }
        case 5:
        {
            juce::PopupMenu sizes;

            for (int c = cmdFont100; c <= cmdFont200; ++c)
                sizes.addCommandItem (cm, c);

            m.addCommandItem (cm, cmdMixer);
            m.addCommandItem (cm, cmdMaster);
            m.addCommandItem (cm, cmdInspector);
            m.addSeparator();
            m.addCommandItem (cm, cmdZoomIn);
            m.addCommandItem (cm, cmdZoomOut);
            m.addCommandItem (cm, cmdAutoScroll);
            m.addSeparator();
            m.addSubMenu ("文字サイズ（画面共有用）"_ju, sizes);
            break;
        }
        case 6:
        {
            // 設定: オーディオ（MIDI 入力もここ）・プラグイン・サーバー・操作モード
            m.addCommandItem (cm, cmdAudioSettings);
            m.addCommandItem (cm, cmdPlugins);
            m.addCommandItem (cm, cmdSyncSettings);
            m.addSeparator();

            juce::PopupMenu modes;
            modes.addCommandItem (cm, cmdModeCubase);
            modes.addCommandItem (cm, cmdModeStudioOne);
            m.addSubMenu ("操作モード"_ju, modes);
            break;
        }
        case 7:
            m.addCommandItem (cm, cmdCredits);
            m.addCommandItem (cm, cmdShortcuts);
            m.addCommandItem (cm, cmdCheckUpdate);
            m.addCommandItem (cm, cmdAbout);
            break;
        default: break;
    }

    return m;
}
