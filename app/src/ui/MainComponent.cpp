#include "MainComponent.h"

#include "Dialogs.h"
#include "SyncUI.h"
#include "Theme.h"
#include "collab/Uuid.h"
#include "sync/SyncManager.h"

namespace
{
    enum Commands
    {
        cmdNew = 0x2000, cmdOpen, cmdSave, cmdUndo, cmdRedo, cmdDelete, cmdSelectAll, cmdDuplicate,
        cmdPlay, cmdToStart, cmdLoop, cmdMetronome, cmdQuantise,
        cmdAddDrums, cmdAddBass, cmdAddPiano,
        cmdAudioSettings, cmdCredits, cmdAbout,
        cmdFont100, cmdFont125, cmdFont150, cmdFont175, cmdFont200,
        cmdSyncSettings, cmdSyncRegister, cmdSyncOpen, cmdSyncPull, cmdSyncPush, cmdSyncHistory, cmdSyncRefreshLocks,
        cmdAddAudioTrack, cmdImportAudio, cmdSplit, cmdPlugins
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

    if (! library.getLoadErrors().isEmpty())
        setStatus ("内蔵音源の読み込みエラー: "_ju + library.getLoadErrors().joinIntoString ("; "));
    else if (library.getAll().empty())
        setStatus ("内蔵音源（assets フォルダ）が見つかりません。音が鳴りません。"_ju);
    else
        setStatus ("準備完了"_ju);

    updateTitle();
    startTimerHz (30);
    setSize (1400, 860);
}

MainComponent::~MainComponent()
{
    bridge.onPluginRemoved = nullptr;
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

    if (playing)
        timeline.followPlayhead (tick);
}

void MainComponent::updateTitle()
{
    auto title = toJuce (document.getProject().name);

    if (document.isDirty())
        title = "* " + title;

    title << " - CollabDAW";

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
                   "（Windows で ASIO を使うには ASIO SDK 付きでビルドする必要があります: M2）"_ju,
                   juce::dontSendNotification);
    note->setFont (juce::FontOptions (12.0f));
    note->setColour (juce::Label::textColourId, Theme::textDim);

    selector->setBounds (0, 0, 560, 420);
    note->setBounds (8, 424, 544, 48);

    struct Holder : juce::Component
    {
        std::unique_ptr<juce::Component> a, b;
    };

    auto holder = std::make_unique<Holder>();
    holder->a = std::move (selector);
    holder->b = std::move (note);
    holder->addAndMakeVisible (*holder->a);
    holder->addAndMakeVisible (*holder->b);
    holder->setSize (560, 480);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (holder.release());
    o.dialogTitle = "オーディオ設定"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::showCredits()
{
    juce::String text = "CollabDAW はオープンソースの JUCE / Tracktion Engine（GPL）、sfizz（BSD-2-Clause）を使用しています。\n\n"_ju
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
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.addArray ({ cmdNew, cmdOpen, cmdSave, cmdUndo, cmdRedo, cmdDelete, cmdSelectAll, cmdDuplicate,
                         cmdPlay, cmdToStart, cmdLoop, cmdMetronome, cmdQuantise,
                         cmdAddDrums, cmdAddBass, cmdAddPiano, cmdAudioSettings, cmdCredits, cmdAbout,
                         cmdFont100, cmdFont125, cmdFont150, cmdFont175, cmdFont200,
                         cmdSyncSettings, cmdSyncRegister, cmdSyncOpen, cmdSyncPull, cmdSyncPush, cmdSyncHistory, cmdSyncRefreshLocks,
                         cmdAddAudioTrack, cmdImportAudio, cmdSplit, cmdPlugins });
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
        case cmdToStart:    info.setInfo ("先頭へ"_ju, {}, "Transport", 0); info.addDefaultKeypress (KP::homeKey, 0); break;
        case cmdLoop:
            info.setInfo ("ループ"_ju, {}, "Transport", 0);
            info.addDefaultKeypress ('l', 0);
            info.setTicked (state.loopEnabled);
            break;
        case cmdMetronome:
            info.setInfo ("メトロノーム"_ju, {}, "Transport", 0);
            info.addDefaultKeypress ('c', 0);
            info.setTicked (state.metronomeEnabled);
            break;
        case cmdAddDrums:   info.setInfo ("MIDIトラックを追加（ドラム）"_ju, {}, "Track", 0); break;
        case cmdAddBass:    info.setInfo ("MIDIトラックを追加（ベース）"_ju, {}, "Track", 0); break;
        case cmdAddPiano:   info.setInfo ("MIDIトラックを追加（ピアノ）"_ju, {}, "Track", 0); break;
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
        case cmdAbout:      info.setInfo ("CollabDAW について…"_ju, {}, "Help", 0); break;
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
        case cmdLoop:       state.loopEnabled = ! state.loopEnabled; state.changed(); break;
        case cmdMetronome:  state.metronomeEnabled = ! state.metronomeEnabled; state.changed(); break;
        case cmdAddDrums:   ctx.addBuiltinMidiTrack (collab::builtin::drums, "Drums"); break;
        case cmdAddBass:    ctx.addBuiltinMidiTrack (collab::builtin::bass, "Bass"); break;
        case cmdAddPiano:   ctx.addBuiltinMidiTrack (collab::builtin::piano, "Piano"); break;
        case cmdAddAudioTrack: ctx.addAudioTrack ("Audio"); break;
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
            Dialogs::showInfo ("CollabDAW について"_ju,
                               "CollabDAW "_ju + juce::String (JUCE_APPLICATION_VERSION_STRING)
                                 + "\n共同制作用の軽量DAW（アイデア出し・ラフ録音用）"_ju);
            break;
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
            break;
        case 2:
            m.addCommandItem (cm, cmdPlay);
            m.addCommandItem (cm, cmdToStart);
            m.addCommandItem (cm, cmdLoop);
            m.addCommandItem (cm, cmdMetronome);
            break;
        case 3:
            m.addCommandItem (cm, cmdAddDrums);
            m.addCommandItem (cm, cmdAddBass);
            m.addCommandItem (cm, cmdAddPiano);
            m.addCommandItem (cm, cmdAddAudioTrack);
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

            m.addSubMenu ("文字サイズ（画面共有用）"_ju, sizes);
            break;
        }
        case 6:
            m.addCommandItem (cm, cmdCredits);
            m.addCommandItem (cm, cmdAbout);
            break;
        default: break;
    }

    return m;
}
