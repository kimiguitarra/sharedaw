#include "MainComponent.h"

#include "Dialogs.h"
#include "ChannelStripEditor.h"
#include "MixerView.h"
#include "SyncUI.h"
#include "Theme.h"
#include "collab/ClipEditing.h"
#include "collab/Uuid.h"
#include "audio/Takes.h"
#include "sync/SyncManager.h"

namespace
{
    enum Commands
    {
        cmdNew = 0x2000, cmdOpen, cmdSave, cmdUndo, cmdRedo, cmdDelete, cmdSelectAll, cmdDuplicate,
        cmdPlay, cmdToStart, cmdLoop, cmdMetronome, cmdQuantise,
        cmdAddDrums, cmdAddBass, cmdAddPiano,
        cmdAudioSettings, cmdCredits, cmdAbout, cmdCheckUpdate,
        cmdFont100, cmdFont125, cmdFont150, cmdFont175, cmdFont200,
        cmdSyncSettings, cmdSyncRegister, cmdSyncOpen, cmdSyncPull, cmdSyncPush, cmdSyncHistory, cmdSyncRefreshLocks,
        cmdAddAudioTrack, cmdImportAudio, cmdSplit, cmdPlugins,
        cmdRecord, cmdCountIn0, cmdCountIn1, cmdCountIn2,
        cmdToolSelect, cmdToolPencil, cmdModeCubase, cmdModeStudioOne, cmdMixer, cmdLoopToSelection,
        cmdStop, cmdZoomIn, cmdZoomOut, cmdSnap, cmdAutoScroll
    };

    constexpr float fontScales[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
    constexpr int transportHeight = 46;
    constexpr int statusHeight = 22;
}

MainComponent::MainComponent (te::Engine& e, ProjectDocument& d, EngineBridge& b,
                              const InstrumentLibrary& lib, SyncManager& s, juce::PropertiesFile& props)
    : engine (e), document (d), bridge (b), library (lib), sync (s), settings (props)
{
    addAndMakeVisible (transport);
    addAndMakeVisible (timeline);
    addAndMakeVisible (pianoRoll);
    addAndMakeVisible (statusBar);

    resizer = std::make_unique<juce::StretchableLayoutResizerBar> (&layout, 1, false);
    addAndMakeVisible (*resizer);

    layout.setItemLayout (0, 120, -1.0, -0.55);   // タイムライン
    layout.setItemLayout (1, 6, 6, 6);            // 仕切り
    layout.setItemLayout (2, 150, -1.0, -0.45);   // ピアノロール

    statusBar.setFont (juce::FontOptions (12.0f));
    statusBar.setColour (juce::Label::textColourId, Theme::textDim);
    statusBar.setColour (juce::Label::backgroundColourId, Theme::panel);

    transport.onAudioSettings = [this] { showAudioSettings(); };
    audioCache.onThumbnailChanged = [this] { timeline.repaint(); };

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
    sync.onLockRequired = [this] (std::vector<std::string> ids) { requestLocks (std::move (ids)); };
    ctx.addLockMenuItems = [this] (const std::string& id, juce::PopupMenu& m) { lockMenuForScope (id, m); };
    ctx.toggleRecord = [this] { toggleRecord(); };

    state.countInBars = juce::jlimit (0, 2, settings.getIntValue ("countInBars", 1));
    state.mode = settings.getValue ("operationMode") == "studioOne" ? OperationMode::studioOne : OperationMode::cubase;
    commandManager.getKeyMappings()->resetToDefaultMappings();
    ctx.addTrackMenu = [this] { return addTrackMenu(); };
    ctx.openChannelStrip = [this] (const std::string& id) { openChannelStrip (id); };
    bridge.onRecordingFinished = [this] (std::vector<EngineBridge::RecordedTake> takes) { importTakes (std::move (takes)); };
    engine.getDeviceManager().deviceManager.addChangeListener (this);
    applyLatencyOffset();

    if (! library.getLoadErrors().isEmpty())
        setStatus ("内蔵音源の読み込みエラー: "_ju + library.getLoadErrors().joinIntoString ("; "));
    else if (library.getAll().empty())
        setStatus ("内蔵音源（assets フォルダ）が見つかりません。音が鳴りません。"_ju);
    else
        setStatus ("準備完了"_ju);

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
    stripWindow = nullptr;
    mixerWindow = nullptr;
    bridge.onPluginRemoved = nullptr;
    bridge.onRecordingFinished = nullptr;
    engine.getDeviceManager().deviceManager.removeChangeListener (this);
    pluginWindows.closeAll();
    sync.onLockRequired = nullptr;
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
    transport.setBounds (area.removeFromTop (transportHeight));
    statusBar.setBounds (area.removeFromBottom (statusHeight));

    juce::Component* comps[] = { &timeline, resizer.get(), &pianoRoll };
    layout.layOutComponents (comps, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);
}

void MainComponent::setStatus (const juce::String& text)
{
    statusBar.setText ("  " + text, juce::dontSendNotification);
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
            state.selectedClipId = {};
            state.changed();
        }
        else if (ctx.selectedClip() == nullptr && ! state.selectedClipId.empty())
        {
            state.selectedClipId = {};
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
}

void MainComponent::timerCallback()
{
    const bool playing = bridge.isPlaying();
    const double tick = bridge.getPositionTick();
    state.playheadTick = tick;

    transport.updatePosition (tick, bridge.getPositionSeconds(), playing);
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
            state.selectedTrackId = state.selectedClipId = {};
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
        state.selectedTrackId = state.selectedClipId = {};
        state.timeline.scrollTick = 0;
        state.changed();
        bridge.returnToStart();
        settings.setValue ("lastProjectDir", folder.getFullPathName());
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
            setStatus ("保存しました: "_ju + document.getProjectFile().getFullPathName());
        }

        if (onDone)
            onDone (r.wasOk());
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

    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (dm, 0, 2, 0, 2, true, false, true, false);
    auto note = std::make_unique<juce::Label>();
    note->setText ("プロジェクトのサンプルレートは 48kHz 固定です。可能ならデバイスも 48000 Hz に設定してください。"_ju
                   "（Windows で ASIO を使うには ASIO SDK 付きでビルドする必要があります）"_ju,
                   juce::dontSendNotification);
    note->setFont (juce::FontOptions (12.0f));
    note->setColour (juce::Label::textColourId, Theme::textDim);

    selector->setBounds (0, 0, 560, 420);
    note->setBounds (8, 424, 544, 48);

    // レイテンシ補正（§3.5）: ドライバが報告する値で自動補正し、さらにデバイスごとに手動でずらせる
    struct Latency  : public juce::Component,
                      private juce::ChangeListener
    {
        Latency (MainComponent& o) : owner (o)
        {
            title.setText ("録音のレイテンシ補正（手動、サンプル）"_ju, juce::dontSendNotification);
            title.setFont (juce::FontOptions (13.0f));
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

            info.setFont (juce::FontOptions (12.0f));
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
    latency->setBounds (0, 476, 560, 64);

    struct Holder : juce::Component
    {
        std::unique_ptr<juce::Component> a, b, c;
    };

    auto holder = std::make_unique<Holder>();
    holder->a = std::move (selector);
    holder->b = std::move (note);
    holder->c = std::move (latency);
    holder->addAndMakeVisible (*holder->a);
    holder->addAndMakeVisible (*holder->b);
    holder->addAndMakeVisible (*holder->c);
    holder->setSize (560, 546);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (holder.release());
    o.dialogTitle = "オーディオ設定"_ju;
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

    if (! document.hasLocation())
        return Dialogs::showInfo ("録音"_ju, "録音した音はプロジェクトのフォルダに保存するので、先にプロジェクトを保存してください。"_ju);

    std::vector<std::string> armed;

    for (auto& t : document.getProject().tracks)
        if (auto in = bridge.getTrackInput (t.id); in.armed && in.device.isNotEmpty())
            armed.push_back (t.id);

    if (armed.empty())
        return Dialogs::showInfo ("録音"_ju, "録音するオーディオトラックの録音待機ボタン（●）をオンにしてください。"_ju
                                             "トラックがなければ「トラック → オーディオトラックを追加」で作れます。"_ju);

    // 同期中はロックを持っているトラックにだけ録音できる（§4.2）
    std::vector<std::string> notEditable;

    for (auto& id : armed)
        if (! sync.canEdit (id))
            notEditable.push_back (id);

    if (! notEditable.empty())
        return requestLocks (std::move (notEditable));

    if (auto r = bridge.startRecording (state.countInBars); r.failed())
        return Dialogs::showError ("録音できません"_ju, r.getErrorMessage());

    setStatus ("録音中（停止で確定）"_ju);
    commandManager.commandStatusChanged();
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

    if (! state.selectedClipId.empty() && ctx.selectedTrack() != nullptr)
    {
        auto trackId = state.selectedTrackId, clipId = state.selectedClipId;
        document.perform ("クリップの削除"_ju, [trackId, clipId] (collab::Project& p)
        {
            if (auto* t = p.findTrack (trackId))
            {
                std::erase_if (t->midiClips, [&] (auto& c) { return c.id == clipId; });
                std::erase_if (t->audioClips, [&] (auto& c) { return c.id == clipId; });
            }
        });
    }
}

void MainComponent::duplicateClip()
{
    auto* clip = ctx.selectedClip();

    if (clip == nullptr)
        return;

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

    state.selectedClipId = copy.id;
    state.changed();
}

//==============================================================================
juce::PopupMenu MainComponent::addTrackMenu()
{
    juce::PopupMenu m, instruments;
    instruments.addCommandItem (&commandManager, cmdAddDrums);
    instruments.addCommandItem (&commandManager, cmdAddBass);
    instruments.addCommandItem (&commandManager, cmdAddPiano);

    m.addCommandItem (&commandManager, cmdAddAudioTrack);
    m.addSubMenu ("音源トラックを追加"_ju, instruments);
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
    setStatus (mode == OperationMode::studioOne ? "Studio One モード（いまは Cubase と同じ操作）"_ju : "Cubase モード"_ju);
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

        if (auto* top = getTopLevelComponent())
            window->setTopLeftPosition (top->getX() + 80, top->getBottom() - window->getHeight() - 60);

        mixerWindow = std::move (window);
    }

    mixerWindow->setVisible (! mixerWindow->isVisible());

    if (mixerWindow->isVisible())
        mixerWindow->toFront (true);

    commandManager.commandStatusChanged();
}

void MainComponent::openChannelStrip (const std::string& trackId)
{
    if (document.getProject().findTrack (trackId) == nullptr)
        return;

    // 1 つのウィンドウを使い回し、開くトラックを切り替える
    if (stripWindow == nullptr)
    {
        struct Window  : public juce::DocumentWindow
        {
            Window() : DocumentWindow ("EQ / コンプ"_ju, Theme::panel, DocumentWindow::closeButton) {}
            void closeButtonPressed() override      { setVisible (false); }
        };

        auto window = std::make_unique<Window>();
        window->setUsingNativeTitleBar (true);
        auto* editor = new ChannelStripEditor (ctx, trackId);
        auto* w = window.get();
        editor->onTitleChanged = [w, editor] { w->setName (editor->getTitle()); };
        window->setContentOwned (editor, true);
        window->setResizable (false, false);
        window->addKeyListener (commandManager.getKeyMappings());

        if (auto* top = getTopLevelComponent())
            window->setTopLeftPosition (top->getX() + 120, top->getY() + 120);

        stripWindow = std::move (window);
    }

    if (auto* editor = dynamic_cast<ChannelStripEditor*> (stripWindow->getContentComponent()))
    {
        editor->setTrack (trackId);
        stripWindow->setName (editor->getTitle());
    }

    stripWindow->setVisible (true);
    stripWindow->toFront (true);
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
        return setStatus ("ループ範囲にするクリップ（またはノート）を選択してください"_ju);

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
                         cmdAddDrums, cmdAddBass, cmdAddPiano, cmdAudioSettings, cmdCredits, cmdAbout, cmdCheckUpdate,
                         cmdRecord, cmdCountIn0, cmdCountIn1, cmdCountIn2,
                         cmdFont100, cmdFont125, cmdFont150, cmdFont175, cmdFont200,
                         cmdSyncSettings, cmdSyncRegister, cmdSyncOpen, cmdSyncPull, cmdSyncPush, cmdSyncHistory, cmdSyncRefreshLocks,
                         cmdAddAudioTrack, cmdImportAudio, cmdSplit, cmdPlugins,
                         cmdToolSelect, cmdToolPencil, cmdModeCubase, cmdModeStudioOne, cmdMixer, cmdLoopToSelection,
                         cmdStop, cmdZoomIn, cmdZoomOut, cmdSnap, cmdAutoScroll });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using KP = juce::KeyPress;
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const float currentScale = juce::Desktop::getInstance().getGlobalScaleFactor();

    switch (id)
    {
        case cmdNew:        info.setInfo ("新規プロジェクト…"_ju, {}, "File", 0); info.addDefaultKeypress ('n', cmd); break;
        case cmdOpen:       info.setInfo ("開く…"_ju, {}, "File", 0); info.addDefaultKeypress ('o', cmd); break;
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
        case cmdDuplicate:  info.setInfo ("クリップを複製"_ju, {}, "Edit", 0); info.addDefaultKeypress ('d', cmd); break;
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
            info.addDefaultKeypress ('r', 0);
            info.defaultKeypresses.add (state.behaviour().recordKey);
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
        case cmdLoopToSelection:
            info.setInfo ("ループ範囲を選択範囲に合わせる"_ju, {}, "Transport", 0);
            info.defaultKeypresses.add (state.behaviour().loopToSelectionKey);
            break;
        case cmdPlugins:       info.setInfo ("プラグイン（スキャン・一覧）…"_ju, {}, "Options", 0); break;
        case cmdAddAudioTrack: info.setInfo ("オーディオトラックを追加"_ju, {}, "Track", 0); break;
        case cmdImportAudio:   info.setInfo ("オーディオを読み込む…"_ju, {}, "File", 0); info.addDefaultKeypress ('i', cmd); break;
        case cmdSplit:         info.setInfo ("再生位置で分割"_ju, {}, "Edit", 0); info.addDefaultKeypress ('s', 0); break;
        case cmdAudioSettings: info.setInfo ("オーディオ設定…"_ju, {}, "Options", 0); break;
        case cmdSyncSettings:  info.setInfo ("サーバー設定…"_ju, {}, "Sync", 0); break;
        case cmdSyncRegister:  info.setInfo ("このプロジェクトをサーバーに登録…"_ju, {}, "Sync", 0); info.setActive (! sync.isLinked()); break;
        case cmdSyncOpen:      info.setInfo ("サーバーから開く…"_ju, {}, "Sync", 0); break;
        case cmdSyncPull:      info.setInfo ("取り込み（pull）…"_ju, {}, "Sync", 0); info.setActive (sync.isLinked()); break;
        case cmdSyncPush:      info.setInfo ("アップロード（push）…"_ju, {}, "Sync", 0); info.setActive (sync.isLinked()); break;
        case cmdSyncHistory:   info.setInfo ("リビジョン履歴…"_ju, {}, "Sync", 0); info.setActive (sync.isLinked()); break;
        case cmdSyncRefreshLocks: info.setInfo ("ロックの状態を更新"_ju, {}, "Sync", 0); info.setActive (sync.isLinked()); break;
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
        case cmdNew:        newProject(); break;
        case cmdOpen:       openProject(); break;
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
        case cmdSnap:
            state.setSnapEnabled (! state.snapEnabled());
            setStatus (state.snapEnabled() ? "スナップ: オン（クオンタイズ値に合わせる）"_ju : "スナップ: オフ（フリー）"_ju);
            break;
        case cmdAutoScroll:
            state.autoScroll = ! state.autoScroll;
            state.changed();
            setStatus (state.autoScroll ? "自動スクロール: オン"_ju : "自動スクロール: オフ"_ju);
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
        case cmdAddAudioTrack: ctx.addAudioTrack ("Audio"); break;
        case cmdToolSelect:    state.tool = EditTool::select; state.changed(); break;
        case cmdToolPencil:    state.tool = EditTool::pencil; state.changed(); break;
        case cmdModeCubase:    setOperationMode (OperationMode::cubase); break;
        case cmdModeStudioOne: setOperationMode (OperationMode::studioOne); break;
        case cmdMixer:         toggleMixer(); break;
        case cmdLoopToSelection: loopToSelection(); break;
        case cmdPlugins:       showPluginManager(); break;
        case cmdImportAudio:   importAudio(); break;
        case cmdSplit:         ctx.splitAtPlayhead(); break;
        case cmdAudioSettings: showAudioSettings(); break;
        case cmdSyncSettings:  showServerSettings(); break;
        case cmdSyncRegister:  registerProject(); break;
        case cmdSyncOpen:      openFromServer(); break;
        case cmdSyncPull:      pull(); break;
        case cmdSyncPush:      push(); break;
        case cmdSyncHistory:   showHistory(); break;
        case cmdSyncRefreshLocks:
        {
            auto r = SyncUI::runWithProgress ("ロックを確認しています"_ju, [this] { return sync.fetchLocks(); });
            if (r.failed()) Dialogs::showError ("取得できませんでした"_ju, r.getErrorMessage());
            break;
        }
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
    return { "ファイル"_ju, "編集"_ju, "トランスポート"_ju, "トラック"_ju, "同期"_ju, "表示"_ju, "ヘルプ"_ju };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    auto* cm = &commandManager;

    switch (index)
    {
        case 0:
            m.addCommandItem (cm, cmdNew);
            m.addCommandItem (cm, cmdOpen);
            m.addCommandItem (cm, cmdSave);
            m.addSeparator();
            m.addCommandItem (cm, cmdImportAudio);
            m.addSeparator();
            m.addCommandItem (cm, cmdAudioSettings);
            m.addCommandItem (cm, cmdPlugins);
           #if ! JUCE_MAC
            m.addSeparator();
            m.addCommandItem (cm, juce::StandardApplicationCommandIDs::quit);
           #endif
            break;
        case 1:
            m.addCommandItem (cm, cmdUndo);
            m.addCommandItem (cm, cmdRedo);
            m.addSeparator();
            m.addCommandItem (cm, cmdDelete);
            m.addCommandItem (cm, cmdSelectAll);
            m.addCommandItem (cm, cmdDuplicate);
            m.addCommandItem (cm, cmdSplit);
            m.addCommandItem (cm, cmdQuantise);
            m.addSeparator();
            m.addCommandItem (cm, cmdToolSelect);
            m.addCommandItem (cm, cmdToolPencil);
            m.addCommandItem (cm, cmdSnap);
            break;
        case 2:
            m.addCommandItem (cm, cmdPlay);
            m.addCommandItem (cm, cmdStop);
            m.addCommandItem (cm, cmdRecord);
            m.addCommandItem (cm, cmdToStart);
            m.addCommandItem (cm, cmdLoop);
            m.addCommandItem (cm, cmdLoopToSelection);
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
            m.addCommandItem (cm, cmdSyncPull);
            m.addCommandItem (cm, cmdSyncPush);
            m.addSeparator();
            m.addCommandItem (cm, cmdSyncHistory);
            m.addCommandItem (cm, cmdSyncRefreshLocks);

            if (sync.isLinked())
            {
                const auto& p = document.getProject();

                for (auto [id, name] : { std::pair (p.tempoTrack.id, "テンポのロック"_ju), std::pair (p.meterTrack.id, "拍子のロック"_ju),
                                         std::pair (p.chordTrack.id, "コードのロック"_ju) })
                {
                    juce::PopupMenu sub;
                    lockMenuForScope (id, sub);
                    auto label = name;

                    if (auto lock = sync.getLock (id))
                        label << "（"_ju << lock->displayName << "）"_ju;

                    m.addSubMenu (label, sub);
                }
            }

            m.addSeparator();
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
            m.addSeparator();
            m.addCommandItem (cm, cmdZoomIn);
            m.addCommandItem (cm, cmdZoomOut);
            m.addCommandItem (cm, cmdAutoScroll);
            m.addSeparator();

            juce::PopupMenu modes;
            modes.addCommandItem (cm, cmdModeCubase);
            modes.addCommandItem (cm, cmdModeStudioOne);
            m.addSubMenu ("操作モード"_ju, modes);
            m.addSubMenu ("文字サイズ（画面共有用）"_ju, sizes);
            break;
        }
        case 6:
            m.addCommandItem (cm, cmdCredits);
            m.addCommandItem (cm, cmdCheckUpdate);
            m.addCommandItem (cm, cmdAbout);
            break;
        default: break;
    }

    return m;
}
