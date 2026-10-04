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
        document.load (folder);
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

void MainComponent::exportMixdown (ExportKind kind)
{
    // 既定の保存先: プロジェクトのフォルダの横に「曲名.wav / .mp3 / .mid」（パラデータは「曲名_stems」フォルダ）
    const auto name = toJuce (document.getProject().name).trim();
    const auto base = juce::File::createLegalFileName (name.isEmpty() ? juce::String ("mixdown") : name);
    const auto dir = document.hasLocation() ? document.getProjectDir().getParentDirectory()
                                            : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    struct Choice { juce::String title, extension, pattern; };
    const Choice choice = kind == ExportKind::mp3   ? Choice { "ミックスダウンを書き出す（MP3）"_ju, ".mp3", "*.mp3" }
                        : kind == ExportKind::midi  ? Choice { "MIDI ファイルを書き出す"_ju, ".mid", "*.mid" }
                        : kind == ExportKind::stems ? Choice { "パラデータを書き出す（フォルダを作る場所と名前）"_ju, "_stems", "" }
                                                    : Choice { "ミックスダウンを書き出す（WAV）"_ju, ".wav", "*.wav" };

    chooser = std::make_unique<juce::FileChooser> (choice.title, dir.getChildFile (base + choice.extension), choice.pattern);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                            | (kind == ExportKind::stems ? 0 : juce::FileBrowserComponent::warnAboutOverwriting),
                          [this, kind] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == juce::File())
            return;

        juce::Result result = juce::Result::ok();
        juce::String done;
        juce::MouseCursor::showWaitCursor();

        switch (kind)
        {
            case ExportKind::wav:
                file = file.withFileExtension ("wav");
                result = Export::mixdownWav (bridge, document, file);
                done = file.getFullPathName() + "\nに書き出しました（48 kHz / 24 bit WAV）。\n\n"_ju + Export::loudnessSummary (file);
                break;

            case ExportKind::mp3:
                file = file.withFileExtension ("mp3");
                result = Export::mixdownMp3 (bridge, document, file);
                done = file.getFullPathName() + "\nに書き出しました（44.1 kHz / 320 kbps MP3）。\n\n"_ju + Export::loudnessSummary (file);
                break;

            case ExportKind::midi:
                file = file.withFileExtension ("mid");
                result = Export::midi (document, file);
                done = file.getFullPathName() + "\nに書き出しました（MIDI トラックとコード、テンポ・拍子・キー・マーカー）。"_ju;
                break;

            case ExportKind::stems:
            {
                // 選んだ名前のフォルダを作って、その中にトラックごとの WAV を置く（同じ名前があれば番号を付ける）
                auto folder = file.getParentDirectory().getNonexistentChildFile (file.getFileNameWithoutExtension(), {}, false);
                juce::Array<juce::File> written;
                result = Export::stems (bridge, document, folder, written);
                done = folder.getFullPathName() + "\nに "_ju + juce::String (written.size())
                       + " 本書き出しました（48 kHz / 24 bit WAV、全部同じ長さ）。\n\n"_ju
                       + "インサート・EQ・Comp・音量・パンを含み、センド・バス・マスターのリミッターは含みません。ミュート中のトラックは書き出しません。"_ju;
                break;
            }
        }

        juce::MouseCursor::hideWaitCursor();

        if (result.failed())
            return Dialogs::showError ("書き出し"_ju, result.getErrorMessage());

        Dialogs::showInfo ("書き出し"_ju, done.trimEnd());
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
