#include "MainComponent.h"
#include "AudioFilesPanel.h"
#include "collab/Automation.h"
#include "collab/Render.h"
#include "MainComponentCommands.h"

#include <iostream>

#include "Dialogs.h"
#include "BuiltinEffectEditor.h"
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
#include "audio/Export.h"
#include "audio/Takes.h"
#include "sync/SyncManager.h"

using namespace MainCommands;

namespace
{
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
    syncPanel.onWhoUploaded = [this] (const std::string& scopeId, const juce::String& name) { showLastUpload (scopeId, name); };
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
    state.autoArmSelected = settings.getBoolValue ("autoArmSelected", true);
    audioCache.transientsAtPeak = settings.getBoolValue ("transientsAtPeak", false);

    // インスペクター・トラックヘッダー・同期パネルの幅は境目をドラッグで変える（この PC の設定）。
    // 作業する場所（タイムライン）を広く取れるよう、最初は細めにしておく
    inspectorWidth = juce::jlimit (Inspector::minWidth, Inspector::maxWidth, settings.getIntValue ("inspectorWidth", Inspector::defaultWidth));
    timeline.setHeaderWidth (settings.getIntValue ("trackHeaderWidth", 170));
    syncPanel.setExpandedWidth (settings.getIntValue ("syncPanelWidth", 240));
    timeline.onHeaderWidthChanged = [this] { settings.setValue ("trackHeaderWidth", timeline.getHeaderWidth()); };

    auto setupResizer = [this] (PaneResizer& r, std::function<int()> get, std::function<void (int)> set, const char* key)
    {
        r.onResize = [this, &r, get, set] (int dx)
        {
            if (resizeStartWidth == 0)
                resizeStartWidth = get();

            set (resizeStartWidth + (&r == &syncResizer ? -dx : dx));   // 同期パネルは左の端を動かす
            resized();
        };
        r.onResizeEnd = [this, get, key]
        {
            resizeStartWidth = 0;
            settings.setValue (key, get());
        };
        addAndMakeVisible (r);
    };

    setupResizer (inspectorResizer, [this] { return inspectorWidth; },
                  [this] (int w) { inspectorWidth = juce::jlimit (Inspector::minWidth, Inspector::maxWidth, w); }, "inspectorWidth");
    setupResizer (syncResizer, [this] { return syncPanel.getExpandedWidth(); },
                  [this] (int w) { syncPanel.setExpandedWidth (w); }, "syncPanelWidth");

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
        if (openBuiltinEffect (trackId, effectId))
            return;

        if (auto* plugin = bridge.getExternalPlugin (trackId, effectId))
            pluginWindows.show (*plugin, plugin->getName());
        else if (bridge.isPlayingRender (trackId))
            Dialogs::showInfo ("プラグイン"_ju, "この環境ではプラグインを鳴らせないため、バウンスした音で再生しています。"_ju);
    };
    ctx.openPluginEditorSoon = [this] (const std::string& trackId)
    {
        // エンジンへの反映（プラグインの読み込み）は変更通知の後なので、読み込まれるまで少し待つ
        auto attempt = std::make_shared<std::function<void (int)>>();
        *attempt = [safe = juce::Component::SafePointer<MainComponent> (this), trackId, weak = std::weak_ptr (attempt)] (int left)
        {
            if (safe == nullptr)
                return;

            if (auto* plugin = safe->bridge.getExternalPlugin (trackId))
                safe->pluginWindows.show (*plugin, plugin->getName());
            else if (left > 0)
                if (auto next = weak.lock())
                    juce::Timer::callAfterDelay (200, [next, left] { (*next) (left - 1); });
        };
        juce::Timer::callAfterDelay (100, [attempt] { (*attempt) (10); });
    };
    bridge.onPluginRemoved = [this] (te::Plugin* p) { pluginWindows.closeFor (p); };

    // プラグインの画面（Superior Drummer など）を操作している間も、Space などで再生できるように
    pluginWindows.keyListener = &numpadKeys;
    pluginWindows.onKey = [this] (const juce::KeyPress& key)
    {
        if (const auto command = commandManager.getKeyMappings()->findCommandForKeyPress (key); command != 0)
            commandManager.invokeDirectly (command, true);
    };
    timeline.onOpenClip = [this] { pianoRoll.focusEditor(); };
    transport.onMixer = [this] { toggleMixer(); };
    transport.onPianoFull = [this] { togglePianoFullScreen(); };

    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    addKeyListener (&numpadKeys);
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
    ctx.openAudioFiles = [this] { showAudioFiles(); };
    ctx.openChannelStrip = [this] (const std::string& id, bool compressor) { openChannelStrip (id, compressor); };
    ctx.openMaster = [this] { openMaster(); };
    bridge.onRecordingFinished = [this] (std::vector<EngineBridge::RecordedTake> takes) { importTakes (std::move (takes)); };
    bridge.onMidiRecorded = [this] (std::vector<EngineBridge::RecordedMidi> recs) { importMidiRecording (std::move (recs)); };
    bridge.onTrackInputsChanged = [this] { saveTrackInputs(); };
    engine.getDeviceManager().deviceManager.addChangeListener (this);
    applyLatencyOffset();

    // 画面の下に文字の行は出さない（ボタンの並びが崩れる）。困ったことだけダイアログで知らせる
    if (! library.getLoadErrors().isEmpty())
        Dialogs::showError ("内蔵音源の読み込みエラー"_ju, library.getLoadErrors().joinIntoString ("\n"));
    else if (library.getAll().empty())
        Dialogs::showError ("内蔵音源が見つかりません"_ju, "内蔵音源が見つかりません。アプリを入れ直してください。"_ju);

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

    if (pianoFullScreen)
        togglePianoFullScreen();

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
    syncPanel.setBounds (area.removeFromRight (juce::jmin (syncPanel.getPreferredWidth(), area.getWidth() / 2)));
    syncResizer.setVisible (! syncPanel.isCollapsed());
    syncResizer.setBounds (syncPanel.getX() - 3, syncPanel.getY(), 6, syncPanel.getHeight());

    // 左にインスペクター（選択中のトラックのチャンネルストリップ。Alt+I で表示 / 非表示）
    if (inspector.isVisible())
        inspector.setBounds (area.removeFromLeft (inspectorWidth));

    inspectorResizer.setVisible (inspector.isVisible());
    inspectorResizer.setBounds (inspector.getRight() - 3, inspector.getY(), 6, inspector.getHeight());

    toast.setTopLeftPosition (area.getRight() - toast.getWidth() - 12, area.getBottom() - toast.getHeight() - 12);

    if (pianoFullScreen)
    {
        // ピアノロールは別のウィンドウに出しているので、ここはタイムラインだけ
        resizer->setVisible (false);
        timeline.setBounds (area);
    }
    else
    {
        resizer->setVisible (true);
        juce::Component* comps[] = { &timeline, resizer.get(), &pianoRoll };
        layout.layOutComponents (comps, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);
    }
    inspectorResizer.toFront (false);
    syncResizer.toFront (false);
}

void MainComponent::runSmokeSteps (const juce::File& project, std::function<void()> done)
{
    struct Step { int delayMs; std::function<void (MainComponent&)> action; const char* name; };

    auto steps = std::make_shared<std::vector<Step>>();
    steps->push_back ({ 500, [project] (MainComponent& m) { if (project.isDirectory()) m.openProjectFolder (project); }, "open project" });
    steps->push_back ({ 1500, [] (MainComponent& m)
    {
        // メニューを全部作り直す（Mac はここでメニューバーの項目とショートカットを登録し直す）
        for (int i = 0; i < m.getMenuBarNames().size(); ++i)
            m.getMenuForIndex (i, m.getMenuBarNames()[i]);

        m.menuItemsChanged();
        m.commandManager.commandStatusChanged();
    }, "rebuild menus" });
    steps->push_back ({ 800, [] (MainComponent& m)
    {
        // 最初の MIDI クリップを選んで、ピアノロールの画面とミキサーを開く
        for (auto& t : m.document.getProject().tracks)
            if (! t.midiClips.empty())
            {
                m.state.selectedTrackId = t.id;
                m.state.selectClip (t.midiClips.front().id);
                m.state.changed();
                break;
            }

        m.togglePianoFullScreen();
        m.toggleMixer();
    }, "open piano roll window and mixer" });
    steps->push_back ({ 800, [] (MainComponent& m)
    {
        // 五線譜の表示に切り替える（SHAREDAW_SMOKE_SHOTS があれば、確認用に画面を保存する）
        m.pianoRoll.setStaffMode (true);
        m.pianoRoll.repaint();
        const auto dir = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_SMOKE_SHOTS", {});

        auto shoot = [&] (const char* name)
        {
            if (dir.isEmpty() || m.pianoRoll.getWidth() <= 0)
                return;

            juce::FileOutputStream out (juce::File (dir).getChildFile (name));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (m.pianoRoll.createComponentSnapshot (m.pianoRoll.getLocalBounds()), out);
        };

        shoot ("staff.png");

        // ミキサー（マスターのインサートも）
        if (dir.isNotEmpty() && m.mixerWindow != nullptr)
            if (auto* mixer = m.mixerWindow->getContentComponent(); mixer != nullptr && mixer->getWidth() > 0)
            {
                juce::FileOutputStream out (juce::File (dir).getChildFile ("mixer.png"));
                out.setPosition (0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream (mixer->createComponentSnapshot (mixer->getLocalBounds()), out);
            }

        m.pianoRoll.setStaffBassClef (false);
        shoot ("staff-treble.png");
        m.pianoRoll.setStaffBassClef (true);
        m.pianoRoll.setStaffMode (false);
    }, "staff view" });
    steps->push_back ({ 1500, [] (MainComponent& m) { m.togglePianoFullScreen(); m.toggleMixer(); }, "close piano roll window and mixer" });
    steps->push_back ({ 300, [] (MainComponent& m)
    {
        // ピアノロールの編集: 全部選んでナッジ・ベロシティを変え、元に戻す
        auto* clip = m.ctx.selectedClip();

        if (clip == nullptr || clip->notes.empty())
            return;

        const auto before = *clip;
        m.pianoRoll.selectAllNotes();
        m.pianoRoll.nudgeSelection (1);
        m.pianoRoll.changeSelectedVelocity (-5);
        auto* after = m.ctx.selectedClip();
        const auto& n0 = before.notes.front();
        auto it = std::find_if (after->notes.begin(), after->notes.end(), [&] (auto& n) { return n.id == n0.id; });

        if (it == after->notes.end() || it->tick != std::min (n0.tick + m.pianoRoll.nudgeTicks, before.lengthTick - 1)
             || it->velocity != juce::jlimit (1, 127, n0.velocity - 5))
        {
            std::cout << "smoke: FAILED nudge / velocity" << std::endl;
            std::_Exit (7);
        }

        m.document.undo();
        m.document.undo();

        if (m.ctx.selectedClip()->notes.front().tick != before.notes.front().tick)
        {
            std::cout << "smoke: FAILED undo of nudge" << std::endl;
            std::_Exit (7);
        }
    }, "nudge and velocity in the piano roll" });
    steps->push_back ({ 300, [] (MainComponent& m)
    {
        // ピッチベンド: ドラム以外の MIDI クリップを選んで、下のピッチベンドの段に描き（ドラッグと同じ編集）、画面を保存し、元に戻す
        for (auto& t : m.document.getProject().tracks)
            if (t.type == collab::TrackType::midi && ! t.midiClips.empty()
                 && ! (t.instrument && t.instrument->id == "builtin.drums"))
            {
                m.state.selectedTrackId = t.id;
                m.state.selectClip (t.midiClips.front().id);
                m.state.changed();
                break;
            }

        m.pianoRoll.resized();   // ドラムからほかの楽器に変わったので、段を並べ直す（ふだんは変更の通知で行う）
        auto* clip = m.ctx.selectedClip();

        if (clip == nullptr)
            return;

        m.pianoRoll.showPitchBend (true);
        const auto length = clip->lengthTick;
        m.pianoRoll.editNotes ("ピッチベンド"_ju, [length] (collab::MidiClip& c)
        {
            std::vector<collab::PitchBend> points;

            for (collab::Tick t = 0; t < length / 2; t += 60)
                points.push_back ({ t, (int) (8191.0 * std::sin ((double) t / (double) length * 6.28)) });

            collab::replacePitchBends (c.pitchBends, 0, length / 2, points);
        });

        if (auto* after = m.ctx.selectedClip(); after == nullptr || after->pitchBends.empty())
        {
            std::cout << "smoke: FAILED pitch bend was not added" << std::endl;
            std::_Exit (7);
        }

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_SMOKE_SHOTS", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("pitchbend.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (m.pianoRoll.createComponentSnapshot (m.pianoRoll.getLocalBounds()), out);
        }

        m.document.undo();
        m.pianoRoll.showPitchBend (false);

        if (auto* after = m.ctx.selectedClip(); after != nullptr && ! after->pitchBends.empty())
        {
            std::cout << "smoke: FAILED undo of pitch bend" << std::endl;
            std::_Exit (7);
        }
    }, "pitch bend lane" });
    steps->push_back ({ 300, [] (MainComponent& m)
    {
        m.commandManager.invokeDirectly (cmdWaveBigger, false);
        m.commandManager.invokeDirectly (cmdWaveBigger, false);
        m.timeline.repaintLanes();
    }, "waveform display zoom" });
    steps->push_back ({ 300, [] (MainComponent& m)
    {
        for (int i = 0; i < 4; ++i)
            m.commandManager.invokeDirectly (cmdWaveSmaller, false);

        if (m.state.waveformZoom != 1.0f)
        {
            std::cout << "smoke: FAILED waveform zoom did not return to 1" << std::endl;
            std::_Exit (7);
        }
    }, "waveform display zoom back" });
    steps->push_back ({ 800, [] (MainComponent& m)
    {
        m.openMaster();   // 再生中のリミッターの表示（波形・ゲインリダクション）
        m.commandManager.invokeDirectly (cmdPlay, false);
    }, "play" });
    steps->push_back ({ 300, [] (MainComponent& m)
    {
        // 再生しながら選択トラックを切り替える（録音待機・MIDI の入力先も付いて移る。音が途切れないこと）
        juce::Component::SafePointer<MainComponent> safe (&m);

        for (int i = 0; i < 6; ++i)
            juce::Timer::callAfterDelay (250 * i, [safe, i]
            {
                if (safe == nullptr)
                    return;

                const auto& tracks = safe->document.getProject().tracks;

                if (! tracks.empty())
                {
                    safe->state.selectedTrackId = tracks[(size_t) i % tracks.size()].id;
                    safe->state.changed();
                }
            });
    }, "switch the selected track while playing" });
    steps->push_back ({ 2000, [] (MainComponent& m)
    {
        // 確認用: マスターの画面を保存する
        const auto dir = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_SMOKE_SHOTS", {});

        if (dir.isNotEmpty() && m.masterWindow != nullptr)
            if (auto* content = m.masterWindow->getContentComponent())
            {
                juce::FileOutputStream out (juce::File (dir).getChildFile ("master.png"));
                out.setPosition (0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream (content->createComponentSnapshot (content->getLocalBounds()), out);
            }

        if (m.masterWindow != nullptr)
            m.masterWindow->setVisible (false);

        m.commandManager.invokeDirectly (cmdPlay, false);
        m.bridge.stop();
    }, "stop" });
    steps->push_back ({ 100, [] (MainComponent& m)
    {
        // オートメーション: ベースのトラックに音量のレーンを出して点を置く（下のタイムラインの画面に写る）
        for (auto& t : m.document.getProject().tracks)
            if (t.name == "Bass")
            {
                m.state.automationShown[t.id] = "volume";
                m.ctx.editTrack (t.id, "オートメーション"_ju, [] (collab::Track& tr)
                {
                    collab::setAutomation (tr, "volume", { { 0, -24.0 }, { collab::kPpq * 4, 0.0 }, { collab::kPpq * 6, -6.0 } });
                });
                m.state.changed();
            }
    }, "automation lane" });
    steps->push_back ({ 100, [] (MainComponent& m)
    {
        // オートメーションのレーンをマウスで: クリックで点を足し、ドラッグで動かし、ダブルクリックで消す
        auto& lanes = m.timeline.getLanes();
        const auto& tracks = m.document.getProject().tracks;
        int row = -1;

        for (size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].name == "Piano")
                row = (int) i;

        if (row < 0)
            return;

        const auto trackId = tracks[(size_t) row].id;
        m.state.automationShown[trackId] = "pan";
        m.state.tool = EditTool::select;
        m.state.changed();

        const int top = lanes.rowTop (row) - lanes.scrollY + m.state.clipLaneHeight (trackId);
        const float y = (float) top + 10.0f;   // 上のほう = 右
        const float x = (float) m.state.timeline.tickToX ((double) collab::kPpq * 4);
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        auto event = [&] (juce::Point<float> p, int clicks, bool dragged)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, p, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &lanes, &lanes, now, { x, y }, now, clicks, dragged);
        };

        auto points = [&]
        {
            auto* t = m.document.getProject().findTrack (trackId);
            auto* lane = t != nullptr ? t->findAutomation ("pan") : nullptr;
            return lane != nullptr ? lane->points : std::vector<collab::AutomationPoint>();
        };

        lanes.mouseDown (event ({ x, y }, 1, false));
        lanes.mouseUp (event ({ x, y }, 1, false));
        const auto added = points();
        const bool addedOk = added.size() == 1 && added[0].tick == collab::kPpq * 4 && added[0].value > 0.5;

        lanes.mouseDown (event ({ x, y }, 1, false));
        lanes.mouseDrag (event ({ x + 40.0f, y + 30.0f }, 1, true));
        lanes.mouseUp (event ({ x + 40.0f, y + 30.0f }, 1, true));
        const auto moved = points();
        const bool movedOk = moved.size() == 1 && moved[0].tick > collab::kPpq * 4 && moved[0].value < added[0].value;

        const float x2 = (float) m.state.timeline.tickToX ((double) moved[0].tick);
        const float y2 = top + 6.0f + (1.0f - (float) ((moved[0].value + 1.0) / 2.0)) * ((float) EditorState::automationLaneHeight - 1.0f - 12.0f);
        lanes.mouseDown (event ({ x2, y2 }, 2, false));
        lanes.mouseUp (event ({ x2, y2 }, 2, false));
        const bool deletedOk = points().empty();

        std::cout << "automation edit: " << (addedOk && movedOk && deletedOk ? "ok" : "FAILED")
                  << " (added " << addedOk << " moved " << movedOk << " deleted " << deletedOk << ")" << std::endl;
        m.state.automationShown.erase (trackId);
        m.state.changed();
    }, "automation editing" });
    steps->push_back ({ 300, [] (MainComponent& m)
    {
        // 確認用: タイムラインの画面（波形・立ち上がりの線）を保存する
        const auto dir = juce::SystemStats::getEnvironmentVariable ("SHAREDAW_SMOKE_SHOTS", {});

        if (dir.isNotEmpty() && m.timeline.getWidth() > 0)
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("timeline.png"));
            out.setPosition (0);
            out.truncate();
            // 試験用の画面は小さいので、一時的に高くして全部のトラックを写す
            const auto old = m.timeline.getBounds();
            m.timeline.setSize (juce::jmax (old.getWidth(), 900), juce::jmax (old.getHeight(), 640));
            juce::PNGImageFormat().writeImageToStream (m.timeline.createComponentSnapshot (m.timeline.getLocalBounds()), out);
            m.timeline.setBounds (old);
        }
    }, "timeline snapshot" });
    steps->push_back ({ 300, [] (MainComponent& m)
    {
        // 書き出し（画面から: 進み具合のバーを出して、WAV と MP3 と MIDI をまとめて）
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sharedaw-smoke-export");
        dir.deleteRecursively();
        juce::StringArray done, failed;
        m.exportFiles (true, true, false, true, dir, "smoke", done, failed);
        bool ok = failed.isEmpty() && dir.getChildFile ("smoke.wav").getSize() > 1000
                         && dir.getChildFile ("smoke.mp3").getSize() > 1000 && dir.getChildFile ("smoke.mid").existsAsFile();
        // 範囲（2 小節目の頭から 3 小節目の頭まで）: ちょうど 1 小節の長さ（余韻を足さない）
        const auto& map = m.document.getTempoMap();
        const Export::Range range { map.barToTick (2), map.barToTick (3) };
        juce::StringArray rangeDone, rangeFailed;
        m.exportFiles (true, false, false, false, dir, "range", rangeDone, rangeFailed, &range);
        double rangeSeconds = 0.0;

        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (juce::WavAudioFormat().createReaderFor (dir.getChildFile ("range.wav").createInputStream().release(), true)))
            rangeSeconds = (double) reader->lengthInSamples / reader->sampleRate;

        const double expected = map.tickToSeconds ((double) range.end) - map.tickToSeconds ((double) range.start);
        const bool rangeOk = rangeFailed.isEmpty() && std::abs (rangeSeconds - expected) < 0.02;
        std::cout << "export range: " << rangeSeconds << " s (expected " << expected << ")" << std::endl;
        ok = ok && rangeOk;

        std::cout << "export panel: " << (ok ? "ok" : "FAILED " + failed.joinIntoString ("; ")) << std::endl;
        dir.deleteRecursively();
    }, "export" });
    steps->push_back ({ 100, [] (MainComponent& m)
    {
        // オーディオファイルの一覧: 使っていないファイルだけが「使っていない」になる
        if (! m.document.hasLocation())
            return;

        const auto audioDir = m.document.getProjectDir().getChildFile ("audio");
        audioDir.createDirectory();
        const auto stray = audioDir.getChildFile ("Guitar_take09_0123abcd.wav");
        stray.replaceWithText ("x");
        const auto unused = AudioFilesPanel::unusedFiles (m.document);
        bool usedListed = false;

        for (auto& t : m.document.getProject().tracks)
            for (auto& c : t.audioClips)
                usedListed = usedListed || unused.contains (AudioFiles::fileForHash (m.document.getProjectDir(), c.audioHash));

        std::cout << "audio files: " << (unused.contains (stray) && ! usedListed ? "ok" : "FAILED") << std::endl;
        stray.deleteFile();
    }, "audio files" });

    // 手順を順に、間を空けて実行する（画面が作り直されて this が消えたら止める。そのときは done を呼ばない＝CI は時間切れで失敗する）
    struct Runner
    {
        static void next (juce::Component::SafePointer<MainComponent> safe, std::shared_ptr<std::vector<Step>> steps,
                          size_t index, std::function<void()> done)
        {
            if (index >= steps->size())
                return done();

            // 手順は、待った後の 1 回きりのメッセージで行う（callAfterDelay のタイマーの中で行うと、手順の中で進み具合の窓（モーダル）を
            // 出している間に、同じタイマーがもう一度呼ばれて消えてしまう）
            juce::Timer::callAfterDelay ((*steps)[index].delayMs, [safe, steps, index, done]
            {
                juce::MessageManager::callAsync ([safe, steps, index, done] { run (safe, steps, index, done); });
            });
        }

        static void run (juce::Component::SafePointer<MainComponent> safe, std::shared_ptr<std::vector<Step>> steps,
                         size_t index, std::function<void()> done)
        {
            {
                if (safe == nullptr)
                {
                    std::cout << "smoke: main window went away before " << (*steps)[index].name << std::endl;
                    return;
                }

                std::cout << "smoke: " << (*steps)[index].name << std::endl;
                (*steps)[index].action (*safe);
                next (safe, steps, index + 1, done);
            }
        }
    };

    Runner::next (juce::Component::SafePointer<MainComponent> (this), steps, 0, std::move (done));
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
        // 消したエフェクトの画面は閉じる
        for (auto it = effectWindows.begin(); it != effectWindows.end();)
        {
            bool exists = false;

            for (auto& t : document.getProject().tracks)
                for (auto& e : t.effects)
                    exists = exists || e.id == it->first;

            for (auto& e : document.getProject().master.effects)
                exists = exists || e.id == it->first;

            it = exists ? std::next (it) : effectWindows.erase (it);
        }

        updateTitle();
        commandManager.commandStatusChanged();

        // 外部プラグインのトラックをバウンスしたトラックは、持ち主の PC では表示しない（元のトラックで編集・再生する）
        std::set<std::string> hidden;

        for (auto& t : document.getProject().tracks)
            if (collab::isHiddenBounceTrack (document.getProject(), t))
                hidden.insert (t.id);

        if (hidden != state.hiddenTracks)
        {
            state.hiddenTracks = hidden;

            if (state.isHidden (state.selectedTrackId))
                state.selectedTrackId = {};

            state.changed();
            resized();
        }

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

    followSelectionWithRecordArm();

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
    // タイムラインとピアノロールの横の拡大・縮小を連動させる（どちらかを変えたら、もう片方も同じ倍率で）
    {
        auto& tl = state.timeline.pixelsPerQuarter;
        auto& pr = state.pianoRoll.pixelsPerQuarter;

        // 連動した側は、再生位置の見える場所が動かないように広げる・縮める
        auto follow = [this] (TimeAxis& axis, double newPpq)
        {
            const double x = axis.tickToX (state.playheadTick);
            axis.pixelsPerQuarter = newPpq;
            axis.scrollTick = juce::jmax (0.0, state.playheadTick - x * collab::kPpq / newPpq);
        };

        // クリップを選んだときにピアノロール（下の編集欄）がクリップ全体に合わせたのは、タイムラインに伝えない
        // （タイムラインで拡大して細かく作業している途中に、クリップを触っただけで縮小されないように）
        if (std::exchange (state.pianoRollAutoFitted, false))
            lastPianoZoom = pr;

        if (lastTimelineZoom > 0.0 && std::abs (tl - lastTimelineZoom) > 1.0e-9)
        {
            follow (state.pianoRoll, juce::jlimit (10.0, TimeAxis::maxPixelsPerQuarter, pr * tl / lastTimelineZoom));
            state.changed();
        }
        else if (lastPianoZoom > 0.0 && std::abs (pr - lastPianoZoom) > 1.0e-9)
        {
            follow (state.timeline, juce::jlimit (4.0, TimeAxis::maxPixelsPerQuarter, tl * pr / lastPianoZoom));
            state.changed();
        }

        lastTimelineZoom = tl;
        lastPianoZoom = pr;
    }

    bridge.pollMidiActivity();
    bridge.pollRecording();

    if (bridge.isRecording() || ! bridge.getLiveRecordings().empty() || wasRecording)
        timeline.repaintLanes();   // 録音中の音を描き足す

    wasRecording = bridge.isRecording();
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

    juce::PopupMenu plugins;

    for (auto& d : PluginHost::list (engine, true))
        plugins.addItem (d.name + " (" + d.manufacturerName + ")", [this, d] { ctx.addExternalMidiTrack (d); });

    if (plugins.getNumItems() == 0)
        plugins.addItem ("プラグインがありません（設定 → プラグイン… でスキャン）"_ju, false, false, nullptr);

    instruments.addSeparator();
    instruments.addSubMenu ("外部プラグイン"_ju, plugins);

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

void MainComponent::zoom (double factor)
{
    // ピアノロールにフォーカスがあればピアノロール、それ以外はタイムライン。再生位置を中心に拡大・縮小する
    const bool piano = pianoRoll.hasKeyboardFocus (true);
    auto& axis = piano ? state.pianoRoll : state.timeline;
    const double x = axis.tickToX (bridge.getPositionTick());
    const double width = piano ? pianoRoll.getWidth() : timeline.getWidth() - timeline.getHeaderWidth();
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
