// MainComponent のファイル: 曲を開く・保存する、読み込み（オーディオ・MIDI）、書き出し

#include "MainComponent.h"
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

void MainComponent::openProjectFolder (const juce::File& folder)
{
    try
    {
        bridge.stop();
        const SyncUI::BusyOverlay busy (this, "曲を開いています…"_ju);
        document.load (folder);
        bridge.sync();   // 音源・プラグインの読み込み（時間がかかる）も、窓を出している間に行う
        state.selectedTrackId = {};
            state.selectClip ({});
        state.timeline.scrollTick = 0;
        state.changed();
        bridge.returnToStart();
        restoreTrackInputs();
        settings.setValue ("lastProjectDir", folder.getFullPathName());
        ProjectPicker::remember (settings, folder);
        setStatus ("開きました: "_ju + folder.getFullPathName());
    }
    catch (const std::exception& e)
    {
        Dialogs::showError ("プロジェクトを開けません"_ju, juce::String::fromUTF8 (e.what()));
    }
}

static juce::String trackInputsKey (const collab::Project& p)
{
    return p.projectId.empty() ? juce::String() : "trackInputs_" + toJuce (p.projectId);
}

void MainComponent::saveTrackInputs()
{
    const auto& project = document.getProject();
    const auto key = trackInputsKey (project);

    if (restoringInputs || key.isEmpty())
        return;

    // 曲のトラックの入力だけ（録音待機は保存しない。開いたときに勝手に録音待機にならないように）
    auto* obj = new juce::DynamicObject();

    for (auto& [trackId, in] : bridge.getTrackInputs())
        if (in.device.isNotEmpty() && project.findTrack (trackId) != nullptr)
            obj->setProperty (toJuce (trackId), juce::Array<juce::var> { in.device, in.deviceRight, in.monitor });

    settings.setValue (key, juce::JSON::toString (juce::var (obj), true));
}

void MainComponent::restoreTrackInputs()
{
    const auto key = trackInputsKey (document.getProject());

    if (key.isEmpty())
        return;

    const auto saved = juce::JSON::parse (settings.getValue (key));

    if (auto* obj = saved.getDynamicObject())
    {
        const juce::ScopedValueSetter<bool> svs (restoringInputs, true);

        for (auto& prop : obj->getProperties())
            if (auto* a = prop.value.getArray(); a != nullptr && a->size() >= 3 && document.getProject().findTrack (prop.name.toString().toStdString()) != nullptr)
                bridge.setTrackInput (prop.name.toString().toStdString(), { (*a)[0].toString(), (*a)[1].toString(), false, (bool) (*a)[2] });
    }

    state.changed();
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

void MainComponent::showExportPanel()
{
    struct Panel  : public juce::Component
    {
        Panel (MainComponent& o) : owner (o)
        {
            title.setText ("書き出す形式"_ju, juce::dontSendNotification);
            title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            addAndMakeVisible (title);

            const std::pair<juce::ToggleButton*, juce::String> items[] = {
                { &wav, "ミックスダウン WAV（48 kHz / 24 bit）"_ju },
                { &mp3, "ミックスダウン MP3（320 kbps）"_ju },
                { &stems, "パラデータ（トラックごとの WAV）"_ju },
                { &midi, "MIDI ファイル"_ju },
            };

            auto& settings = owner.settings;
            const juce::String keys[] = { "exportWav", "exportMp3", "exportStems", "exportMidi" };

            for (size_t i = 0; i < std::size (items); ++i)
            {
                items[i].first->setButtonText (items[i].second);
                items[i].first->setToggleState (settings.getBoolValue (keys[i], i == 0), juce::dontSendNotification);
                items[i].first->onClick = [this] { updateButton(); };
                addAndMakeVisible (items[i].first);
            }

            nameTitle.setText ("名前"_ju, juce::dontSendNotification);
            folderTitle.setText ("保存先"_ju, juce::dontSendNotification);

            for (auto* l : { &nameTitle, &folderTitle })
            {
                l->setFont (juce::FontOptions (15.0f, juce::Font::bold));
                addAndMakeVisible (l);
            }

            const auto songName = toJuce (owner.document.getProject().name).trim();
            name.setText (juce::File::createLegalFileName (songName.isEmpty() ? juce::String ("mixdown") : songName));
            addAndMakeVisible (name);

            folder = juce::File (settings.getValue ("exportFolder"));

            if (! folder.isDirectory())
                folder = owner.document.hasLocation() ? owner.document.getProjectDir().getParentDirectory()
                                                      : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

            folderLabel.setColour (juce::Label::textColourId, Theme::textDim);
            folderLabel.setFont (juce::FontOptions (14.0f));
            folderLabel.setText (folder.getFullPathName(), juce::dontSendNotification);
            addAndMakeVisible (folderLabel);

            chooseFolder.setButtonText ("変更…"_ju);
            chooseFolder.onClick = [this]
            {
                owner.chooser = std::make_unique<juce::FileChooser> ("保存先のフォルダ"_ju, folder);
                owner.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                            [safe = juce::Component::SafePointer<Panel> (this)] (const juce::FileChooser& fc)
                {
                    if (safe != nullptr && fc.getResult().isDirectory())
                    {
                        safe->folder = fc.getResult();
                        safe->folderLabel.setText (safe->folder.getFullPathName(), juce::dontSendNotification);
                    }
                });
            };
            addAndMakeVisible (chooseFolder);

            exportButton.setButtonText ("書き出す"_ju);
            exportButton.onClick = [this]
            {
                auto& st = owner.settings;
                st.setValue ("exportWav", wav.getToggleState());
                st.setValue ("exportMp3", mp3.getToggleState());
                st.setValue ("exportStems", stems.getToggleState());
                st.setValue ("exportMidi", midi.getToggleState());
                st.setValue ("exportFolder", folder.getFullPathName());

                auto* main = &owner;
                const bool w = wav.getToggleState(), m = mp3.getToggleState(), s = stems.getToggleState(), mi = midi.getToggleState();
                const auto dir = folder;
                const auto base = juce::File::createLegalFileName (name.getText().trim());

                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState (0);

                // パネルを閉じてから書き出す（書き出し中は進み具合のバーを出す）
                juce::MessageManager::callAsync ([main, w, m, s, mi, dir, base]
                {
                    main->runExport (w, m, s, mi, dir, base.isEmpty() ? juce::String ("mixdown") : base);
                });
            };
            addAndMakeVisible (exportButton);

            cancelButton.setButtonText ("キャンセル"_ju);
            cancelButton.onClick = [this]
            {
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState (0);
            };
            addAndMakeVisible (cancelButton);

            updateButton();
            setSize (460, 330);
        }

        void updateButton()
        {
            exportButton.setEnabled (wav.getToggleState() || mp3.getToggleState() || stems.getToggleState() || midi.getToggleState());
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (16, 12);
            title.setBounds (area.removeFromTop (24));

            for (auto* b : { &wav, &mp3, &stems, &midi })
                b->setBounds (area.removeFromTop (28).withTrimmedLeft (6));

            area.removeFromTop (10);
            auto row = area.removeFromTop (28);
            nameTitle.setBounds (row.removeFromLeft (70));
            name.setBounds (row.reduced (0, 2));
            area.removeFromTop (6);
            row = area.removeFromTop (28);
            folderTitle.setBounds (row.removeFromLeft (70));
            chooseFolder.setBounds (row.removeFromRight (80).reduced (0, 2));
            folderLabel.setBounds (row.reduced (4, 0));

            auto buttons = area.removeFromBottom (32);
            exportButton.setBounds (buttons.removeFromRight (120));
            buttons.removeFromRight (8);
            cancelButton.setBounds (buttons.removeFromRight (110));
        }

        MainComponent& owner;
        juce::Label title, nameTitle, folderTitle, folderLabel;
        juce::ToggleButton wav, mp3, stems, midi;
        juce::TextEditor name;
        juce::TextButton chooseFolder, exportButton, cancelButton;
        juce::File folder;
    };

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new Panel (*this));
    o.dialogTitle = "書き出し"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.launchAsync();
}

void MainComponent::runExport (bool wav, bool mp3, bool stems, bool midi, const juce::File& folder, const juce::String& name)
{
    juce::StringArray done, failed;
    exportFiles (wav, mp3, stems, midi, folder, name, done, failed);

    if (! failed.isEmpty())
        return Dialogs::showError ("書き出し"_ju, failed.joinIntoString ("\n"));

    Dialogs::showInfo ("書き出し"_ju, folder.getFullPathName() + "\nに書き出しました。\n\n"_ju + done.joinIntoString ("\n\n").trimEnd());
}

void MainComponent::exportFiles (bool wav, bool mp3, bool stems, bool midi, const juce::File& folder, const juce::String& name,
                                 juce::StringArray& done, juce::StringArray& failed)
{
    if (! folder.isDirectory() && ! folder.createDirectory())
    {
        failed.add ("フォルダを作れません: "_ju + folder.getFullPathName());
        return;
    }

    // 同じ名前のファイルがあれば上書きする（パラデータのフォルダは番号を付けて新しく作る）
    if (wav)
    {
        const auto file = folder.getChildFile (name + ".wav");

        if (auto r = Export::mixdownWav (bridge, document, file); r.failed())
            failed.add ("WAV: "_ju + r.getErrorMessage());
        else
            done.add (file.getFileName() + "\n" + Export::loudnessSummary (file));
    }

    if (mp3)
    {
        const auto file = folder.getChildFile (name + ".mp3");
        auto r = Export::mixdownMp3 (bridge, document, file, [] (std::function<juce::Result()> encode)
        {
            return SyncUI::runWithProgress ("MP3 に変換しています…"_ju, std::move (encode));
        });

        if (r.failed())
            failed.add ("MP3: "_ju + r.getErrorMessage());
        else
            done.add (file.getFileName() + "\n" + Export::loudnessSummary (file));
    }

    if (stems)
    {
        auto dir = folder.getNonexistentChildFile (name + "_stems", {}, false);
        juce::Array<juce::File> written;

        if (auto r = Export::stems (bridge, document, dir, written); r.failed())
            failed.add ("パラデータ: "_ju + r.getErrorMessage());
        else
            done.add (dir.getFileName() + "/（"_ju + juce::String (written.size()) + " トラック）"_ju);
    }

    if (midi)
    {
        const auto file = folder.getChildFile (name + ".mid");

        if (auto r = Export::midi (document, file); r.failed())
            failed.add ("MIDI: "_ju + r.getErrorMessage());
        else
            done.add (file.getFileName());
    }
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
