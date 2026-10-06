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
#include "AudioFilesPanel.h"
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
        saveEditorState();   // 今の曲の作業の状態を残してから開く
        const SyncUI::BusyOverlay busy (this, "曲を開いています…"_ju);
        document.load (folder);
        bridge.sync();   // 音源・プラグインの読み込み（時間がかかる）も、窓を出している間に行う
        state.selectedTrackId = {};
            state.selectClip ({});
        state.timeline.scrollTick = 0;
        state.changed();
        bridge.returnToStart();
        restoreTrackInputs();
        restoreEditorState();
        settings.setValue ("lastProjectDir", folder.getFullPathName());
        ProjectPicker::remember (settings, folder);
        setStatus ("開きました: "_ju + folder.getFullPathName());
    }
    catch (const std::exception& e)
    {
        Dialogs::showError ("プロジェクトを開けません"_ju, juce::String::fromUTF8 (e.what()));
    }
}

static juce::String editorStateKey (const collab::Project& p)
{
    return p.projectId.empty() ? juce::String() : "editorState_" + toJuce (p.projectId);
}

juce::String MainComponent::editorStateJson() const
{
    // この PC だけの作業の状態（曲ごと）: ロケーター・ループ、クオンタイズ・スナップ、拡大・スクロール、再生位置、トラックの高さ、オートメーションのレーン
    auto* o = new juce::DynamicObject();
    o->setProperty ("loopEnabled", state.loopEnabled);
    o->setProperty ("loopStart", (juce::int64) state.loopStart);
    o->setProperty ("loopEnd", (juce::int64) state.loopEnd);
    o->setProperty ("gridDivision", state.grid.division);
    o->setProperty ("gridTuplet", state.grid.tuplet);
    o->setProperty ("snap", state.grid.enabled);
    o->setProperty ("timelineZoom", state.timeline.pixelsPerQuarter);
    o->setProperty ("timelineScroll", state.timeline.scrollTick);
    o->setProperty ("pianoZoom", state.pianoRoll.pixelsPerQuarter);
    o->setProperty ("pianoScroll", state.pianoRoll.scrollTick);
    o->setProperty ("waveformZoom", state.waveformZoom);
    o->setProperty ("playhead", state.playheadTick);

    auto* heights = new juce::DynamicObject();

    for (auto& [id, h] : state.trackHeights)
        heights->setProperty (toJuce (id), h);

    o->setProperty ("trackHeights", juce::var (heights));

    auto* automation = new juce::DynamicObject();

    for (auto& [id, param] : state.automationShown)
        automation->setProperty (toJuce (id), toJuce (param));

    o->setProperty ("automation", juce::var (automation));
    return juce::JSON::toString (juce::var (o), true);
}

void MainComponent::saveEditorState()
{
    const auto key = editorStateKey (document.getProject());

    if (key.isEmpty() || ! document.hasLocation())
        return;

    const auto json = editorStateJson();

    if (json != lastSavedEditorState)
    {
        settings.setValue (key, json);
        lastSavedEditorState = json;
    }
}

void MainComponent::restoreEditorState()
{
    const auto key = editorStateKey (document.getProject());
    const auto saved = key.isEmpty() ? juce::var() : juce::JSON::parse (settings.getValue (key));
    auto* o = saved.getDynamicObject();

    if (o == nullptr)
    {
        lastSavedEditorState = editorStateJson();
        return;
    }

    auto get = [o] (const char* name, const juce::var& fallback) { return o->hasProperty (name) ? o->getProperty (name) : fallback; };

    state.loopEnabled = (bool) get ("loopEnabled", state.loopEnabled);
    state.loopStart = (collab::Tick) (juce::int64) get ("loopStart", (juce::int64) state.loopStart);
    state.loopEnd = (collab::Tick) (juce::int64) get ("loopEnd", (juce::int64) state.loopEnd);

    collab::Grid g = state.grid;
    g.division = juce::jlimit (1, 128, (int) get ("gridDivision", g.division));
    g.tuplet = (int) get ("gridTuplet", g.tuplet);
    state.setQuantise (g);
    state.setSnapEnabled ((bool) get ("snap", state.grid.enabled));

    state.timeline.pixelsPerQuarter = juce::jlimit (4.0, TimeAxis::maxPixelsPerQuarter, (double) get ("timelineZoom", state.timeline.pixelsPerQuarter));
    state.timeline.scrollTick = juce::jmax (0.0, (double) get ("timelineScroll", 0.0));
    state.pianoRoll.pixelsPerQuarter = juce::jlimit (10.0, TimeAxis::maxPixelsPerQuarter, (double) get ("pianoZoom", state.pianoRoll.pixelsPerQuarter));
    state.pianoRoll.scrollTick = juce::jmax (0.0, (double) get ("pianoScroll", 0.0));
    lastTimelineZoom = state.timeline.pixelsPerQuarter;   // 開いたときに、タイムラインとピアノロールの拡大を連動させ直さない
    lastPianoZoom = state.pianoRoll.pixelsPerQuarter;
    state.waveformZoom = juce::jlimit (1.0f, 64.0f, (float) (double) get ("waveformZoom", 1.0));

    state.trackHeights.clear();
    state.automationShown.clear();

    if (auto* h = get ("trackHeights", {}).getDynamicObject())
        for (auto& p : h->getProperties())
            if (document.getProject().findTrack (p.name.toString().toStdString()) != nullptr)
                state.trackHeights[p.name.toString().toStdString()] = (int) p.value;

    if (auto* a = get ("automation", {}).getDynamicObject())
        for (auto& p : a->getProperties())
            if (document.getProject().findTrack (p.name.toString().toStdString()) != nullptr)
                state.automationShown[p.name.toString().toStdString()] = p.value.toString().toStdString();

    if (const double playhead = (double) get ("playhead", 0.0); playhead > 0.0)
        bridge.setPositionTick (playhead);

    state.changed();
    lastSavedEditorState = editorStateJson();
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

std::unique_ptr<juce::Component> MainComponent::makeExportPanel()
{
    struct Panel  : public juce::Component
    {
        Panel (MainComponent& o) : owner (o)
        {
            auto& settings = owner.settings;
            const auto& project = owner.document.getProject();
            projectKey = toJuce (project.projectId);

            auto heading = [this] (juce::Label& l, const juce::String& text)
            {
                l.setText (text, juce::dontSendNotification);
                l.setFont (juce::FontOptions (15.0f, juce::Font::bold));
                addAndMakeVisible (l);
            };

            heading (kindTitle, "書き出すもの"_ju);
            heading (formatTitle, "形式"_ju);
            heading (rangeTitle, "範囲"_ju);
            heading (nameTitle, "名前"_ju);
            heading (folderTitle, "保存先"_ju);

            // 何を: ミックスダウン・パラデータ・MIDI（1 つ）。ミックスダウン・パラデータは WAV か MP3
            const std::pair<juce::ToggleButton*, juce::String> kinds[] = {
                { &mixdown, "ミックスダウン"_ju }, { &stems, "パラデータ"_ju }, { &midi, "MIDI"_ju } };
            const std::pair<juce::ToggleButton*, juce::String> formats[] = {
                { &wav, "WAV（48 kHz / 24 bit）"_ju }, { &mp3, "MP3（320 kbps）"_ju } };

            for (auto& [b, text] : kinds)
            {
                b->setButtonText (text);
                b->setRadioGroupId (1);
                b->onClick = [this] { kindChanged(); };
                addAndMakeVisible (b);
            }

            for (auto& [b, text] : formats)
            {
                b->setButtonText (text);
                b->setRadioGroupId (2);
                addAndMakeVisible (b);
            }

            const auto kind = settings.getValue ("exportKind", "mixdown");
            (kind == "stems" ? stems : kind == "midi" ? midi : mixdown).setToggleState (true, juce::dontSendNotification);
            (settings.getValue ("exportAudioFormat", "wav") == "mp3" ? mp3 : wav).setToggleState (true, juce::dontSendNotification);

            // 範囲: 左右のロケーター（既定）か、曲全体（最後の音の余韻まで）
            useRange.setButtonText ("範囲を指定"_ju);
            fullSong.setButtonText ("曲全体"_ju);

            for (auto* b : { &useRange, &fullSong })
            {
                b->setRadioGroupId (3);
                b->onClick = [this] { updateEnabled(); };
                addAndMakeVisible (b);
            }

            (settings.getBoolValue ("exportFullSong", false) ? fullSong : useRange).setToggleState (true, juce::dontSendNotification);

            rangeStart.setText (formatPosition (owner.state.loopStart));
            rangeEnd.setText (formatPosition (owner.state.loopEnd));
            rangeTo.setText ("〜"_ju, juce::dontSendNotification);
            rangeTo.setJustificationType (juce::Justification::centred);

            for (auto* c : std::initializer_list<juce::Component*> { &rangeStart, &rangeEnd, &rangeTo })
                addAndMakeVisible (c);

            // 名前: 書き出すものごとに、この曲で前回書き出した名前（はじめは「YYYYMMDD_曲名_V1-1」など）
            const auto stamp = Export::todayStamp();
            auto songName = juce::File::createLegalFileName (toJuce (project.name).trim());

            if (songName.isEmpty())
                songName = "song";

            auto remembered = [&] (const juce::String& key, const juce::String& fallback)
            {
                const auto v = projectKey.isEmpty() ? juce::String() : settings.getValue (key + projectKey);
                return v.isNotEmpty() ? v : fallback;
            };

            names[0] = remembered ("exportName_", stamp + "_" + songName + "_V1-1");
            names[1] = remembered ("exportStemsName_", stamp + "_" + songName + "_stems");
            names[2] = remembered ("exportMidiName_", stamp + "_" + songName);
            shownKind = kindIndex();
            name.setText (names[(size_t) shownKind]);
            addAndMakeVisible (name);

            // 保存先: この曲で前回のフォルダ（はじめは曲のフォルダ）
            folder = juce::File (projectKey.isEmpty() ? juce::String() : settings.getValue ("exportFolder_" + projectKey));

            if (! folder.isDirectory())
                folder = owner.document.hasLocation() ? owner.document.getProjectDir()
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
            exportButton.onClick = [this] { startExport(); };
            addAndMakeVisible (exportButton);

            cancelButton.setButtonText ("キャンセル"_ju);
            cancelButton.onClick = [this]
            {
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState (0);
            };
            addAndMakeVisible (cancelButton);

            updateEnabled();
            setSize (460, 400);
        }

        int kindIndex() const       { return stems.getToggleState() ? 1 : midi.getToggleState() ? 2 : 0; }

        void kindChanged()
        {
            // 書き出すものを変えたら、名前もそのものの名前に
            names[(size_t) shownKind] = name.getText();
            shownKind = kindIndex();
            name.setText (names[(size_t) shownKind]);
            updateEnabled();
        }

        void updateEnabled()
        {
            // MIDI は形式を選ばず、いつも曲全体
            const bool audio = ! midi.getToggleState();

            for (auto* c : std::initializer_list<juce::Component*> { &wav, &mp3, &useRange, &fullSong, &rangeTo })
                c->setEnabled (audio);

            rangeStart.setEnabled (audio && useRange.getToggleState());
            rangeEnd.setEnabled (audio && useRange.getToggleState());
        }

        void startExport()
        {
            const auto base = juce::File::createLegalFileName (name.getText().trim());

            if (base.isEmpty())
                return Dialogs::showError ("書き出し"_ju, "名前を入れてください。"_ju);

            const int kind = kindIndex();
            const bool audio = kind != 2;
            std::optional<Export::Range> range;

            if (audio && useRange.getToggleState())
            {
                const auto from = parsePosition (rangeStart.getText()), to = parsePosition (rangeEnd.getText());

                if (! from || ! to || *to <= *from)
                    return Dialogs::showError ("書き出し"_ju, "範囲の終わりは始まりより後にしてください。"_ju);

                range = Export::Range { *from, *to };
            }

            // 選んだものと名前・保存先は、次に開いたときの初期値にする（名前・保存先は曲ごと）
            auto& st = owner.settings;
            st.setValue ("exportKind", kind == 1 ? "stems" : kind == 2 ? "midi" : "mixdown");
            st.setValue ("exportAudioFormat", mp3.getToggleState() ? "mp3" : "wav");
            st.setValue ("exportFullSong", fullSong.getToggleState());

            if (projectKey.isNotEmpty())
            {
                const char* keys[] = { "exportName_", "exportStemsName_", "exportMidiName_" };
                st.setValue (keys[kind] + projectKey, base);
                st.setValue ("exportFolder_" + projectKey, folder.getFullPathName());
            }

            auto* main = &owner;
            const auto exportKind = kind == 1 ? ExportKind::stems : kind == 2 ? ExportKind::midi : ExportKind::mixdown;
            const bool asMp3 = audio && mp3.getToggleState();
            const auto dir = folder;

            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);

            // パネルを閉じてから書き出す（書き出し中は進み具合のバーを出す）
            juce::MessageManager::callAsync ([main, exportKind, asMp3, dir, base, range]
            {
                main->runExport (exportKind, asMp3, dir, base, range);
            });
        }

        juce::String formatPosition (collab::Tick t) const
        {
            const auto bb = owner.document.getTempoMap().tickToBarBeat (juce::jmax<collab::Tick> (0, t));
            return juce::String (bb.bar) + ". " + juce::String (bb.beat) + ". " + juce::String (bb.tickInBeat);
        }

        std::optional<collab::Tick> parsePosition (const juce::String& text) const
        {
            // 「小節」「小節.拍」「小節.拍.tick」（トランスポートのロケーターと同じ）
            auto parts = juce::StringArray::fromTokens (text.replaceCharacter (' ', '.'), ".", {});
            parts.removeEmptyStrings();

            if (parts.isEmpty() || parts.size() > 3)
                return std::nullopt;

            const auto& map = owner.document.getTempoMap();
            const int bar = parts[0].getIntValue();

            if (bar < 1)
                return std::nullopt;

            const auto sig = map.timeSignatureAtBar (bar);
            const int beat = juce::jlimit (1, sig.numerator, parts.size() > 1 ? parts[1].getIntValue() : 1);
            const auto tick = juce::jlimit<collab::Tick> (0, sig.ticksPerBeat() - 1, parts.size() > 2 ? parts[2].getIntValue() : 0);
            return map.barToTick (bar) + (collab::Tick) (beat - 1) * sig.ticksPerBeat() + tick;
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (16, 12);

            auto radioRow = [&] (std::initializer_list<std::pair<juce::ToggleButton*, int>> buttons)
            {
                auto row = area.removeFromTop (28).withTrimmedLeft (6);

                for (auto [b, w] : buttons)
                    b->setBounds (row.removeFromLeft (w));
            };

            kindTitle.setBounds (area.removeFromTop (24));
            radioRow ({ { &mixdown, 150 }, { &stems, 130 }, { &midi, 100 } });
            area.removeFromTop (8);
            formatTitle.setBounds (area.removeFromTop (24));
            radioRow ({ { &wav, 200 }, { &mp3, 170 } });
            area.removeFromTop (8);
            rangeTitle.setBounds (area.removeFromTop (24));
            radioRow ({ { &useRange, 130 }, { &fullSong, 160 } });
            auto fields = area.removeFromTop (28).withTrimmedLeft (30);
            rangeStart.setBounds (fields.removeFromLeft (110).reduced (0, 2));
            rangeTo.setBounds (fields.removeFromLeft (30));
            rangeEnd.setBounds (fields.removeFromLeft (110).reduced (0, 2));

            area.removeFromTop (12);
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
        juce::Label kindTitle, formatTitle, rangeTitle, nameTitle, folderTitle, folderLabel, rangeTo;
        juce::ToggleButton mixdown, stems, midi, wav, mp3, useRange, fullSong;
        juce::TextEditor rangeStart, rangeEnd, name;
        juce::TextButton chooseFolder, exportButton, cancelButton;
        juce::File folder;
        juce::String projectKey;
        std::array<juce::String, 3> names;   // ミックスダウン・パラデータ・MIDI の名前
        int shownKind = 0;
    };

    return std::make_unique<Panel> (*this);
}

void MainComponent::showExportPanel()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (makeExportPanel().release());
    o.dialogTitle = "書き出し"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.launchAsync();
}

void MainComponent::runExport (ExportKind kind, bool asMp3, const juce::File& folder, const juce::String& name,
                               std::optional<Export::Range> range)
{
    juce::StringArray done, failed;
    exportFiles (kind, asMp3, folder, name, done, failed, range ? &*range : nullptr);

    if (! failed.isEmpty())
        return Dialogs::showError ("書き出し"_ju, failed.joinIntoString ("\n"));

    Dialogs::showInfo ("書き出し"_ju, folder.getFullPathName() + "\nに書き出しました。\n\n"_ju + done.joinIntoString ("\n\n").trimEnd());
}

void MainComponent::exportFiles (ExportKind kind, bool asMp3, const juce::File& folder, const juce::String& name,
                                 juce::StringArray& done, juce::StringArray& failed, const Export::Range* range)
{
    if (! folder.isDirectory() && ! folder.createDirectory())
    {
        failed.add ("フォルダを作れません: "_ju + folder.getFullPathName());
        return;
    }

    // 同じ名前のファイルがあれば上書きする
    if (kind == ExportKind::mixdown && ! asMp3)
    {
        const auto file = folder.getChildFile (name + ".wav");

        if (auto r = Export::mixdownWav (bridge, document, file, range); r.failed())
            failed.add ("WAV: "_ju + r.getErrorMessage());
        else
            done.add (file.getFileName() + "\n" + Export::loudnessSummary (file));
    }

    if (kind == ExportKind::mixdown && asMp3)
    {
        const auto file = folder.getChildFile (name + ".mp3");
        auto r = Export::mixdownMp3 (bridge, document, file, [] (std::function<juce::Result()> encode)
        {
            return SyncUI::runWithProgress ("MP3 に変換しています…"_ju, std::move (encode));
        }, range);

        if (r.failed())
            failed.add ("MP3: "_ju + r.getErrorMessage());
        else
            done.add (file.getFileName() + "\n" + Export::loudnessSummary (file));
    }

    if (kind == ExportKind::stems)
    {
        // トラックごとのファイルは、名前のフォルダの中に「YYYYMMDD_トラック名」で
        const auto dir = folder.getChildFile (name);
        juce::Array<juce::File> written;

        if (auto r = Export::stems (bridge, document, dir, written, range, Export::todayStamp(), asMp3); r.failed())
            failed.add ("パラデータ: "_ju + r.getErrorMessage());
        else
            done.add (dir.getFileName() + "/（"_ju + juce::String (written.size()) + " トラック）"_ju);
    }

    if (kind == ExportKind::midi)
    {
        const auto file = folder.getChildFile (name + ".mid");

        if (auto r = Export::midi (document, file); r.failed())
            failed.add ("MIDI: "_ju + r.getErrorMessage());
        else
            done.add (file.getFileName());
    }
}

void MainComponent::showAudioFiles()
{
    if (! document.hasLocation())
        return;

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new AudioFilesPanel (document));
    o.dialogTitle = "オーディオファイル"_ju;
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
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
