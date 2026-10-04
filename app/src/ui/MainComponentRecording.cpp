// MainComponent の録音: 録音の開始・停止、MIDI の録音とテイクの取り込み、レイテンシーの補正

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

void MainComponent::followSelectionWithRecordArm()
{
    // 選んだトラックを録音待機にし（R を押したのと同じ）、前に自動で待機にしたトラックは戻す。
    // 自分で R を押して待機にしたトラックはそのまま。録音中は変えない
    if (! state.autoArmSelected || bridge.isRecording() || state.selectedTrackId == autoArmedFor)
        return;

    autoArmedFor = state.selectedTrackId;

    if (! autoArmedTrackId.empty())
        ctx.setRecordArm (std::exchange (autoArmedTrackId, std::string()), false, false);

    if (auto* t = ctx.selectedTrack(); t != nullptr && ! ctx.isRecordArmed (t->id))
    {
        ctx.setRecordArm (t->id, true, false);

        if (ctx.isRecordArmed (t->id))
            autoArmedTrackId = t->id;
    }
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

        for (auto& b : rec.pitchBends)
        {
            first = std::min (first, b.tick);
            last = std::max (last, b.tick + 1);
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

        for (auto b : rec.pitchBends)
            clip.pitchBends.push_back ({ b.tick - clip.startTick, b.value });

        std::stable_sort (clip.pitchBends.begin(), clip.pitchBends.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });

        // 既定では、もうクリップがある所に録ると、そのクリップの続きに入れる（設定で別のクリップにもできる）
        const bool merge = ! settings.getBoolValue ("midiRecordSeparateClips", false);
        const auto trackId = rec.trackId;
        auto targetId = std::make_shared<std::string> (clip.id);
        document.perform ("MIDI の録音"_ju, [trackId, clip, merge, targetId] (collab::Project& p)
        {
            if (auto* t = p.findTrack (trackId))
                *targetId = collab::addRecordedMidi (t->midiClips, clip, merge);
        });

        lastTrack = trackId;
        lastClip = *targetId;
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

    std::map<std::string, juce::String> names;

    for (auto& t : document.getProject().tracks)
        names[t.id] = toJuce (t.name);

    auto r = SyncUI::runWithProgress ("録音を保存しています"_ju, [&] { return Takes::import (takes, dir, map, clips, names); });

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
    bridge.setMidiRecordOffsetMs (settings.getDoubleValue ("midiRecordOffsetMs", 0.0));
}
