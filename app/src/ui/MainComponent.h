#pragma once

#include "update/Updater.h"

#include "ui/AppContext.h"
#include "ui/Inspector.h"
#include "ui/PaneResizer.h"
#include "ui/PianoRoll.h"
#include "ui/PianoRollPage.h"
#include "ui/SyncPanel.h"
#include "ui/TimelineView.h"
#include "ui/TransportBar.h"
#include "collab/ProjectDiff.h"
#include "sync/SyncManager.h"
#include "audio/AudioFiles.h"
#include "plugins/PluginHost.h"
#include "ui/NumpadKeys.h"

/** メインウィンドウの中身。メニュー、ショートカット（コマンド）、各ビューの配置を受け持つ。 */
class MainComponent  : public juce::Component,
                       public juce::ApplicationCommandTarget,
                       public juce::MenuBarModel,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    MainComponent (te::Engine&, ProjectDocument&, EngineBridge&, const InstrumentLibrary&, SyncManager&, juce::PropertiesFile&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** 未保存の変更を確認してから onDone を呼ぶ（新規・開く・終了の前）。 */
    void confirmDiscardChanges (std::function<void()> onDone);

    void openProjectFolder (const juce::File& folder);

    /**
        起動の確認（--smoke-test、CI 用）: 曲を開き、メニューを作り直し、ピアノロールの画面・ミキサーを開閉し、少し再生して止める。
        終わったら done を呼ぶ（途中で落ちれば CI が失敗する）。
    */
    void runSmokeSteps (const juce::File& project, std::function<void()> done);

    juce::ApplicationCommandManager& getCommandManager()     { return commandManager; }

    /** ウィンドウタイトルの更新用。 */
    std::function<void (const juce::String&)> onTitleChanged;

    /** 外観（true = ライト）を切り替えたとき。アプリが画面を作り直す。 */
    std::function<void (bool useLight)> onAppearanceChanged;

    //==============================================================================
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return juce::JUCEApplication::getInstance(); }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override {}

    static void applyFontScale (float scale);

private:
    te::Engine& engine;
    ProjectDocument& document;
    EngineBridge& bridge;
    const InstrumentLibrary& library;
    SyncManager& sync;
    juce::PropertiesFile& settings;

    EditorState state;
    AudioFileCache audioCache;
    AppContext ctx { document, state, bridge, library, sync, audioCache, {}, {}, {}, {}, {}, {} };

    juce::ApplicationCommandManager commandManager;
    NumpadKeys numpadKeys { *commandManager.getKeyMappings() };   // キー割り当ての入口（テンキーのキーを直してから渡す）
    ToolBar toolbar { ctx };
    TransportBar transport { ctx };
    TimelineView timeline { ctx };
    juce::StretchableLayoutManager layout;
    std::unique_ptr<juce::StretchableLayoutResizerBar> resizer;
    PianoRollView pianoRoll { ctx };
    SyncPanel syncPanel { sync, document, settings };
    Inspector inspector { ctx };
    PaneResizer inspectorResizer, syncResizer;
    int inspectorWidth = Inspector::defaultWidth, resizeStartWidth = 0;
    SyncToast toast;
    juce::TooltipWindow tooltips { nullptr, 400 };   // マウスを少し止めるとボタンの説明を出す
    std::unique_ptr<juce::FileChooser> chooser;

    bool lastLoop = false;
    collab::Tick lastLoopStart = -1, lastLoopEnd = -1;
    bool lastMetronome = false;
    float lastMetronomeDb = 0;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void saveProject (std::function<void (bool)> onDone = {});
    void showAudioSettings();

    // 操作（Cubase / Studio One モード、ツール、ループ、ミキサー）
    juce::PopupMenu addTrackMenu();
    void setOperationMode (OperationMode);
    void loopToSelection();
    void zoom (double factor);
    void toggleMixer();
    void showShortcuts();

    // アプリの更新（MainComponentUpdate.cpp）
    void checkForUpdates (bool interactive);
    void offerUpdate (const Updater::Info&, bool interactive);
    void installUpdate (const Updater::Info&);
    std::atomic<bool> updateCheckRunning { false };
    std::unique_ptr<juce::DocumentWindow> mixerWindow;
    void openChannelStrip (const std::string& trackId, bool compressor);

    /** 内蔵エフェクトなら画面を開いて true。 */
    bool openBuiltinEffect (const std::string& trackId, const std::string& effectId);
    std::map<std::string, std::unique_ptr<juce::DocumentWindow>> effectWindows;

    /** MIDI キーボードの録音先（録音待機の MIDI トラック、なければ選択中の MIDI トラック）。 */
    const collab::Track* midiRecordTarget() const;
    std::unique_ptr<juce::DocumentWindow> eqWindow, compWindow;
    void openMaster();
    enum class ExportKind { wav, mp3, stems, midi };
    void exportMixdown (ExportKind);
    std::unique_ptr<juce::DocumentWindow> masterWindow;

    // 録音（§3.5）
    void toggleRecord();
    void importTakes (std::vector<EngineBridge::RecordedTake>);
    juce::String latencySettingKey() const;
    void applyLatencyOffset();
    void showCredits();
    void deleteSelection();
    void duplicateClip();
    void updateTitle();
    void importAudio();
    void importMidi();
    void importMidiRecording (std::vector<EngineBridge::RecordedMidi>);

    /** オーディオトラックの入力の割り当て（この PC の設定。曲ごと）を保存する・曲を開いたときに戻す。 */
    void saveTrackInputs();
    void restoreTrackInputs();
    bool restoringInputs = false;
    void showPluginManager();
    PluginWindows pluginWindows;

    // 同期（§4）
    std::unique_ptr<juce::DocumentWindow> diffWindow;
    void showServerSettings();
    void registerProject();
    void downloadProject (const std::string& projectId);

public:
    /** 「楽曲を選ぶ」画面（起動時にも出す）。 */
    void showProjectPicker();
    void showStretchSongDialog();

private:
    // 同期パネル（右側）から: 画面に出ている差分のまま、すぐに取り込む・アップする
    void toggleSyncPanel();
    bool lastCopiedRange = false;
    double lastTimelineZoom = 0.0, lastPianoZoom = 0.0;   // 拡大・縮小の連動用
    bool wasRecording = false;
    std::string autoArmedTrackId;   // 選んだので自動で録音待機にしたトラック（選択が変わったら戻す）
    std::string autoArmedFor;       // 最後に自動の録音待機を合わせた選択
    void followSelectionWithRecordArm();

    // ピアノロールの画面（ミキサーと同じく別のウィンドウで画面いっぱいに。ピアノロールをそこへ移す）
    bool pianoFullScreen = false;
    std::unique_ptr<juce::DocumentWindow> pianoWindow;
    std::unique_ptr<PianoTopLanes> pianoLanes;
    void togglePianoFullScreen();   // 最後のコピーが上の段の範囲選択だったか（貼り付け先を決める）

    /** 曲を置くフォルダを選んでもらう（キャンセルなら何もしない）。選んだ場所は次の既定になる。 */
    void chooseProjectParent (const juce::String& title, std::function<void (const juce::File&)> onChosen);

    /** 保存済みの曲をサーバーに登録する（registerProject の後半）。 */
    void uploadRegistration();

    /** ダウンロード（競合などの選択つき）。quiet なら自動ダウンロード（バックグラウンド）。取り込めたら true。 */
    bool downloadWithChoices (const std::map<std::string, collab::Resolution>& choices, bool quiet);
    void uploadFromPanel (const std::set<std::string>& excluded, const juce::String& message,
                          const std::map<std::string, collab::Resolution>& choices);
    void uploadAfterBounce (const std::set<std::string>& excluded, const juce::String& message,
                            const std::map<std::string, collab::Resolution>& choices);
    void onIncomingRevisions (const std::vector<SyncManager::RevisionInfo>&);
    void createProjectOnServer();
    bool autoPullRunning = false;
    void showHistory();
    bool ensureSyncReady (bool needLinked);
    void jumpTo (const collab::Change&);
    void setStatus (const juce::String&);
};
