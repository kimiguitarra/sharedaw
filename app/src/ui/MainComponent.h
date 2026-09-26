#pragma once

#include "ui/AppContext.h"
#include "ui/PianoRoll.h"
#include "ui/TimelineView.h"
#include "ui/TransportBar.h"

/** メインウィンドウの中身。メニュー、ショートカット（コマンド）、各ビューの配置を受け持つ。 */
class MainComponent  : public juce::Component,
                       public juce::ApplicationCommandTarget,
                       public juce::MenuBarModel,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    MainComponent (te::Engine&, ProjectDocument&, EngineBridge&, const InstrumentLibrary&, juce::PropertiesFile&);
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
    juce::PropertiesFile& settings;

    EditorState state;
    AppContext ctx { document, state, bridge, library };

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
    void showCredits();
    void deleteSelection();
    void duplicateClip();
    void updateTitle();
    void setStatus (const juce::String&);
};
