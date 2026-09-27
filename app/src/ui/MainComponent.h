#pragma once

#include "update/Updater.h"

#include "ui/AppContext.h"
#include "ui/PianoRoll.h"
#include "ui/TimelineView.h"
#include "ui/TransportBar.h"
#include "collab/ProjectDiff.h"
#include "sync/SyncManager.h"
#include "audio/AudioFiles.h"
#include "plugins/PluginHost.h"

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

    juce::ApplicationCommandManager& getCommandManager()     { return commandManager; }

    /** ウィンドウタイトルの更新用。 */
    std::function<void (const juce::String&)> onTitleChanged;

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
    AppContext ctx { document, state, bridge, library, sync, audioCache, {}, {}, {}, {}, {} };

    juce::ApplicationCommandManager commandManager;
    TransportBar transport { ctx };
    TimelineView timeline { ctx };
    juce::StretchableLayoutManager layout;
    std::unique_ptr<juce::StretchableLayoutResizerBar> resizer;
    PianoRollView pianoRoll { ctx };
    juce::Label statusBar;
    std::unique_ptr<juce::FileChooser> chooser;

    bool lastLoop = false;
    collab::Tick lastLoopStart = -1, lastLoopEnd = -1;
    bool lastMetronome = false;
    float lastMetronomeDb = 0;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void newProject();
    void openProject();
    void saveProject (std::function<void (bool)> onDone = {});
    void showAudioSettings();

    // 操作（Cubase / Studio One モード、ツール、ループ、ミキサー）
    juce::PopupMenu addTrackMenu();
    void setOperationMode (OperationMode);
    void loopToSelection();
    void zoom (double factor);
    void toggleMixer();

    // アプリの更新（MainComponentUpdate.cpp）
    void checkForUpdates (bool interactive);
    void offerUpdate (const Updater::Info&, bool interactive);
    void installUpdate (const Updater::Info&);
    std::atomic<bool> updateCheckRunning { false };
    std::unique_ptr<juce::DocumentWindow> mixerWindow;
    void openChannelStrip (const std::string& trackId);
    std::unique_ptr<juce::DocumentWindow> stripWindow;

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
    void showPluginManager();
    PluginWindows pluginWindows;

    // 同期（§4）
    std::unique_ptr<juce::DocumentWindow> diffWindow;
    void showServerSettings();
    void registerProject();
    void openFromServer();
    void pull();
    void push();
    void showHistory();
    void requestLocks (std::vector<std::string> scopeIds);
    void lockMenuForScope (const std::string& scopeId, juce::PopupMenu&);
    bool ensureSyncReady (bool needLinked);
    void jumpTo (const collab::Change&);
    void applyPullPreview (const SyncManager::PullPreview&);
    void runPushPlan (const SyncManager::PushPlan&, const juce::String& message, bool releaseLocks);
    void closeDiffWindowAsync();
    void setStatus (const juce::String&);
};
