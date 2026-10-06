// MainComponent のコマンド（キーボードショートカット）とメニューバー

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
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.addArray ({ cmdNew, cmdOpen, cmdSave, cmdUndo, cmdRedo, cmdDelete, cmdSelectAll, cmdDuplicate,
                         cmdPlay, cmdToStart, cmdLoop, cmdMetronome, cmdQuantise,
                         cmdAddDrums, cmdAddBass, cmdAddPiano, cmdAddEPiano, cmdAudioSettings, cmdCredits, cmdAbout, cmdCheckUpdate,
                         cmdRecord, cmdCountIn0, cmdCountIn1, cmdCountIn2,
                         cmdFont100, cmdFont125, cmdFont150, cmdFont175, cmdFont200,
                         cmdSyncSettings, cmdSyncRegister, cmdSyncOpen, cmdSyncPull, cmdSyncPush, cmdSyncHistory,
                         cmdAddAudioTrack, cmdImportAudio, cmdImportMidi, cmdExportMixdown, cmdSplit, cmdMuteTrack, cmdSoloTrack, cmdPlugins, cmdArmTrack, cmdTrackHeight,
                         cmdToolSelect, cmdToolPencil, cmdModeCubase, cmdModeStudioOne, cmdMixer, cmdMaster, cmdLoopToSelection,
                         cmdStop, cmdZoomIn, cmdZoomOut, cmdSnap, cmdAutoScroll, cmdAddMarker,
                         cmdMarker1, cmdMarker2, cmdMarker3, cmdMarker4, cmdMarker5, cmdMarker6, cmdMarker7, cmdMarker8, cmdMarker9,
                         cmdToolSplit, cmdCopy, cmdCut, cmdPaste, cmdNudgeLeft, cmdNudgeRight,
                         cmdForward, cmdRewind, cmdShortcuts, cmdSyncPanel, cmdSyncCreate, cmdToLoopStart, cmdToLoopEnd, cmdInspector, cmdCursorLeft, cmdCursorRight, cmdBarLeft, cmdBarRight, cmdTrackUp, cmdTrackDown, cmdPianoFull, cmdWaveBigger, cmdWaveSmaller, cmdTransientsAtPeak, cmdAutoArm, cmdStretchSong, cmdAudioFiles });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using KP = juce::KeyPress;
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const float currentScale = juce::Desktop::getInstance().getGlobalScaleFactor();

    // Mac のメニューはショートカットを文字として登録するので、文字にできないキー（Mac の Insert は -1 など）があると起動時に落ちる。念のため外す
    struct DropUnusableKeys
    {
        juce::ApplicationCommandInfo& i;
        ~DropUnusableKeys()
        {
            i.defaultKeypresses.removeIf ([] (const juce::KeyPress& k)
            {
                const int c = k.getTextCharacter() != 0 ? (int) k.getTextCharacter() : k.getKeyCode();
                return c <= 0 || (c >= 0xd800 && c < 0xe000) || c > 0x10ffff;
            });
        }
    } dropUnusableKeys { info };

    switch (id)
    {
        case cmdNew:        info.setInfo ("新しい曲…"_ju, {}, "File", 0); info.addDefaultKeypress ('n', cmd); break;
        case cmdOpen:       info.setInfo ("楽曲を開く…"_ju, {}, "File", 0); info.addDefaultKeypress ('o', cmd); break;
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
        case cmdDuplicate:  info.setInfo ("複製"_ju, {}, "Edit", 0); info.addDefaultKeypress ('d', cmd); break;
        case cmdCopy:       info.setInfo ("コピー"_ju, {}, "Edit", 0); info.addDefaultKeypress ('c', cmd); break;
        case cmdCut:        info.setInfo ("切り取り"_ju, {}, "Edit", 0); info.addDefaultKeypress ('x', cmd); break;
        case cmdPaste:      info.setInfo ("貼り付け（再生位置へ）"_ju, {}, "Edit", 0); info.addDefaultKeypress ('v', cmd); break;
        case cmdNudgeLeft:  info.setInfo ("クリップを左へずらす"_ju, {}, "Edit", 0); info.addDefaultKeypress (KP::leftKey, cmd); break;
        case cmdNudgeRight: info.setInfo ("クリップを右へずらす"_ju, {}, "Edit", 0); info.addDefaultKeypress (KP::rightKey, cmd); break;
        case cmdForward:    info.setInfo ("1 小節進む"_ju, {}, "Transport", 0); info.addDefaultKeypress (KP::numberPadAdd, 0); break;
        case cmdRewind:     info.setInfo ("1 小節戻る"_ju, {}, "Transport", 0); info.addDefaultKeypress (KP::numberPadSubtract, 0); break;
        case cmdShortcuts:  info.setInfo ("操作とショートカットの一覧…"_ju, {}, "Help", 0); info.addDefaultKeypress (KP::F1Key, 0); break;
        case cmdToLoopStart: info.setInfo ("左ロケーターへ移動"_ju, {}, "Transport", 0); info.addDefaultKeypress (KP::numberPad1, 0); break;
        case cmdCursorLeft:  info.setInfo ("クリップ・再生位置を左へ（グリッド 1 つ）"_ju, {}, "Edit", 0); info.addDefaultKeypress (KP::leftKey, 0); break;
        case cmdCursorRight: info.setInfo ("クリップ・再生位置を右へ（グリッド 1 つ）"_ju, {}, "Edit", 0); info.addDefaultKeypress (KP::rightKey, 0); break;
        case cmdBarLeft:     info.setInfo ("クリップ・再生位置を左へ（1 小節）"_ju, {}, "Edit", 0); info.addDefaultKeypress (KP::leftKey, shift); break;
        case cmdBarRight:    info.setInfo ("クリップ・再生位置を右へ（1 小節）"_ju, {}, "Edit", 0); info.addDefaultKeypress (KP::rightKey, shift); break;
        case cmdTrackUp:     info.setInfo ("上のトラックを選ぶ"_ju, {}, "Track", 0); info.addDefaultKeypress (KP::upKey, 0); break;
        case cmdPianoFull:
            info.setInfo ("ピアノロールを全画面に"_ju, {}, "View", 0);
            info.addDefaultKeypress ('e', 0);
            info.setTicked (pianoFullScreen);
            break;
        case cmdTrackDown:   info.setInfo ("下のトラックを選ぶ"_ju, {}, "Track", 0); info.addDefaultKeypress (KP::downKey, 0); break;
        case cmdToLoopEnd:   info.setInfo ("右ロケーターへ移動"_ju, {}, "Transport", 0); info.addDefaultKeypress (KP::numberPad2, 0); break;
        case cmdToolSplit:
            info.setInfo ("はさみツール"_ju, {}, "Edit", 0);
            info.defaultKeypresses.add (state.behaviour().splitToolKey);
            info.setTicked (state.tool == EditTool::split);
            break;
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
        case cmdWaveBigger:
            info.setInfo ("波形を大きく表示"_ju, {}, "View", 0);
            info.addDefaultKeypress ('h', juce::ModifierKeys::shiftModifier);
            break;
        case cmdWaveSmaller:
            info.setInfo ("波形を小さく表示"_ju, {}, "View", 0);
            info.addDefaultKeypress ('g', juce::ModifierKeys::shiftModifier);
            info.setActive (state.waveformZoom > 1.0f);
            break;
        case cmdTransientsAtPeak:
            // 既定は Cubase・Pro Tools と同じく、音の鳴り始め（アタックの頭）に線を引く
            info.setInfo ("立ち上がりの線を音量が最大の所に引く"_ju, {}, "View", 0);
            info.setTicked (audioCache.transientsAtPeak);
            break;
        case cmdAddMarker:
            info.setInfo ("再生位置にマーカーを追加"_ju, {}, "Transport", 0);
           #if JUCE_MAC
            // Mac のキーボードには Insert がない（JUCE では -1 になり、メニューを作るときに落ちる）ので Shift+Cmd+M
            info.addDefaultKeypress ('m', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
           #else
            info.addDefaultKeypress (KP::insertKey, 0);
           #endif
            break;
        case cmdMarker1: case cmdMarker2: case cmdMarker3: case cmdMarker4: case cmdMarker5:
        case cmdMarker6: case cmdMarker7: case cmdMarker8: case cmdMarker9:
        {
            const int n = (int) id - cmdMarker1 + 1;
            const auto markers = MarkerLane::sorted (document.getProject());
            auto name = "マーカー "_ju + juce::String (n) + " へ移動"_ju;

            if (n <= (int) markers.size() && ! markers[(size_t) n - 1].name.empty())
                name << "（"_ju << toJuce (markers[(size_t) n - 1].name) << "）"_ju;

            info.setInfo (name, {}, "Transport", 0);
            info.addDefaultKeypress ('0' + n, shift);

            // OS やキー配列によっては Shift + 数字が記号として届くので、US / JIS 配列の記号でも受ける
            for (auto* symbols : { "!@#$%^&*(", "!\"#$%&'()" })
                if (const auto c = (juce::juce_wchar) (unsigned char) symbols[n - 1]; c != (juce::juce_wchar) ('0' + n))
                    info.defaultKeypresses.addIfNotAlreadyThere (KP (c, shift, 0));
            info.setActive (n <= (int) markers.size());
            break;
        }
        case cmdSnap:
            info.setInfo ("スナップ"_ju, {}, "Edit", 0);
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
            info.defaultKeypresses.add (state.behaviour().recordKey);
            info.addDefaultKeypress ('*', 0);                              // キーボードの * も（配列によって Shift が付く）
            info.addDefaultKeypress ('*', shift);
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
        case cmdAddEPiano:  info.setInfo ("エレピ"_ju, {}, "Track", 0); break;
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
            info.setInfo ("Studio One モード"_ju, {}, "View", 0);
            info.setTicked (state.mode == OperationMode::studioOne);
            break;
        case cmdMixer:
            info.setInfo ("ミキサー"_ju, {}, "View", 0);
            info.defaultKeypresses.add (state.behaviour().mixerKey);
            info.setTicked (mixerWindow != nullptr && mixerWindow->isVisible());
            break;
        case cmdMaster:
            info.setInfo ("マスター"_ju, {}, "View", 0);
            info.defaultKeypresses.add (juce::KeyPress (juce::KeyPress::F4Key));
            info.setTicked (masterWindow != nullptr && masterWindow->isVisible());
            break;
        case cmdLoopToSelection:
            info.setInfo ("ループ範囲を選択範囲に合わせる"_ju, {}, "Transport", 0);
            info.defaultKeypresses.add (state.behaviour().loopToSelectionKey);
            break;
        case cmdPlugins:       info.setInfo ("プラグイン…"_ju, {}, "Options", 0); break;
        case cmdAddAudioTrack: info.setInfo ("オーディオトラックを追加"_ju, {}, "Track", 0); break;
        case cmdImportAudio:   info.setInfo ("オーディオを読み込む…"_ju, {}, "File", 0); info.addDefaultKeypress ('i', cmd); break;
        case cmdImportMidi:    info.setInfo ("MIDI ファイルを読み込む…"_ju, {}, "File", 0); break;
        case cmdAudioFiles:
            info.setInfo ("オーディオファイルの整理…"_ju, {}, "File", 0);
            info.setActive (document.hasLocation());
            break;
        case cmdExportMixdown:
            info.setInfo ("書き出し…"_ju, {}, "File", 0);
            info.addDefaultKeypress ('e', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case cmdSplit:         info.setInfo ("再生位置で分割"_ju, {}, "Edit", 0); info.addDefaultKeypress ('x', juce::ModifierKeys::altModifier); break;
        case cmdMuteTrack:
            info.setInfo ("選択中のトラックのミュート"_ju, {}, "Track", 0);
            info.addDefaultKeypress ('m', 0);
            info.setActive (ctx.selectedTrack() != nullptr);
            break;
        case cmdSoloTrack:
            info.setInfo ("選択中のトラックのソロ"_ju, {}, "Track", 0);
            info.addDefaultKeypress ('s', 0);
            info.setActive (ctx.selectedTrack() != nullptr);
            break;
        case cmdStretchSong:
            info.setInfo ("曲全体の伸び縮み…"_ju, {}, "Edit", 0);
            break;
        case cmdAutoArm:
            info.setInfo ("選んだトラックを自動で録音待機にする"_ju, {}, "Transport", 0);
            info.setTicked (state.autoArmSelected);
            break;
        case cmdArmTrack:
            info.setInfo ("選択中のトラックの録音待機"_ju, {}, "Track", 0);
            info.addDefaultKeypress ('r', 0);
            info.setActive (ctx.selectedTrack() != nullptr);
            break;
        case cmdTrackHeight:
            info.setInfo ("選択中のトラックの高さを切り替え"_ju, {}, "Track", 0);
            info.addDefaultKeypress ('z', 0);
            info.setActive (ctx.selectedTrack() != nullptr);
            break;
        case cmdAudioSettings: info.setInfo ("オーディオ・MIDI の設定…"_ju, {}, "Options", 0); break;
        case cmdSyncSettings:  info.setInfo ("サーバー設定…"_ju, {}, "Sync", 0); break;
        case cmdSyncRegister:  info.setInfo ("このプロジェクトをサーバーに登録…"_ju, {}, "Sync", 0); info.setActive (! sync.isLinked()); break;
        case cmdSyncOpen:
            info.setInfo ("楽曲を開く…"_ju, {}, "File", 0);
            info.addDefaultKeypress ('o', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;
        case cmdSyncPull:      info.setInfo ("ダウンロード"_ju, {}, "Sync", 0); info.setActive (sync.isLinked()); break;
        case cmdSyncPush:      info.setInfo ("アップロード…"_ju, {}, "Sync", 0); info.setActive (sync.isLinked()); break;
        case cmdSyncHistory:   info.setInfo ("リビジョン履歴…"_ju, {}, "Sync", 0); info.setActive (sync.isLinked()); break;
        case cmdSyncPanel:
            info.setInfo ("同期パネル"_ju, {}, "Sync", 0);
            info.addDefaultKeypress (juce::KeyPress::F7Key, 0);
            info.setTicked (! syncPanel.isCollapsed());
            break;
        case cmdSyncCreate:    info.setInfo ("サーバーに新しい曲を作る…"_ju, {}, "Sync", 0); break;
        case cmdInspector:
            info.setInfo ("インスペクター"_ju, {}, "View", 0);
            info.addDefaultKeypress ('i', juce::ModifierKeys::altModifier);
            info.setTicked (inspector.isVisible());
            break;
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
        case cmdNew:        createProjectOnServer(); break;
        case cmdOpen:       showProjectPicker(); break;
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
        case cmdWaveBigger:
        case cmdWaveSmaller:
            // 表示だけ（クリップの音量や書き出しは変わらない）。1 倍〜16 倍
            state.waveformZoom = juce::jlimit (1.0f, 64.0f, state.waveformZoom * (info.commandID == cmdWaveBigger ? 1.5f : 1.0f / 1.5f));

            if (state.waveformZoom < 1.05f)
                state.waveformZoom = 1.0f;

            state.changed();
            commandManager.commandStatusChanged();
            break;
        case cmdTransientsAtPeak:
            audioCache.transientsAtPeak = ! audioCache.transientsAtPeak;
            settings.setValue ("transientsAtPeak", audioCache.transientsAtPeak);
            timeline.repaint();
            pianoRoll.repaint();
            commandManager.commandStatusChanged();
            break;
        case cmdZoomOut:    zoom (0.8); break;
        case cmdAddMarker:
        {
            const auto t = (collab::Tick) std::llround (state.snapCursor (bridge.getPositionTick(), document.getTempoMap(), {}));
            MarkerLane::addMarker (ctx, t);
            break;
        }
        case cmdMarker1: case cmdMarker2: case cmdMarker3: case cmdMarker4: case cmdMarker5:
        case cmdMarker6: case cmdMarker7: case cmdMarker8: case cmdMarker9:
        {
            const auto markers = MarkerLane::sorted (document.getProject());
            const auto n = (size_t) (info.commandID - cmdMarker1);

            if (n < markers.size())
            {
                bridge.setPositionTick ((double) markers[n].tick);
                timeline.followPlayhead ((double) markers[n].tick);   // 画面もそこへ
                pianoRoll.followPlayhead ((double) markers[n].tick);
                state.selectedMarkerId = markers[n].id;
                state.changed();
            }
            break;
        }
        case cmdSnap:
            state.setSnapEnabled (! state.snapEnabled());
            break;
        case cmdAutoScroll:
            state.autoScroll = ! state.autoScroll;
            state.changed();
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
        case cmdAddEPiano:  ctx.addBuiltinMidiTrack (collab::builtin::epiano, "E.Piano"); break;
        case cmdAddAudioTrack: ctx.addAudioTrack ("Audio"); break;
        case cmdToolSelect:    state.tool = EditTool::select; state.changed(); break;
        case cmdToolPencil:    state.tool = EditTool::pencil; state.changed(); break;
        case cmdToolSplit:     state.tool = EditTool::split; state.changed(); break;
        case cmdCopy:
        case cmdCut:
            if (pianoRoll.hasKeyboardFocus (true) && pianoRoll.hasSelectedNotes())
                pianoRoll.copySelectedNotes (info.commandID == cmdCut);
            else if (timeline.copyRange (info.commandID == cmdCut))
                lastCopiedRange = true;
            else if (timeline.copyChord (info.commandID == cmdCut))
                lastCopiedRange = false;
            else
            {
                ctx.copyClips (state.clipSelection());

                if (info.commandID == cmdCut)
                    ctx.deleteClips (state.clipSelection());
            }
            break;
        case cmdPaste:
            if (pianoRoll.hasKeyboardFocus (true) && pianoRoll.hasNotesInClipboard())
                pianoRoll.pasteNotes();
            else if (lastCopiedRange && timeline.pasteRange (bridge.getPositionTick()))
                break;
            else if (timeline.pasteChord (bridge.getPositionTick()))
                break;
            else
                ctx.pasteClips ((collab::Tick) std::llround (state.snapCursor (bridge.getPositionTick(), document.getTempoMap(), {})));
            break;
        case cmdNudgeLeft:
        case cmdNudgeRight:
        {
            const auto step = std::max<collab::Tick> (1, state.grid.stepTicks());
            ctx.nudgeClips (state.clipSelection(), info.commandID == cmdNudgeLeft ? -step : step);
            break;
        }
        case cmdToLoopStart:   bridge.setPositionTick ((double) state.loopStart); break;
        case cmdCursorLeft: case cmdCursorRight: case cmdBarLeft: case cmdBarRight:
        {
            // クリップを選んでいればクリップを、いなければ再生位置を動かす（Shift で 1 小節）。
            // ピアノロールの中では（ノートを選んでいないとき）クリップは動かさず再生位置を動かす
            const int dir = info.commandID == cmdCursorLeft || info.commandID == cmdBarLeft ? -1 : 1;
            const bool bar = info.commandID == cmdBarLeft || info.commandID == cmdBarRight;
            const auto& map = document.getTempoMap();
            const auto pos = (collab::Tick) std::llround (bridge.getPositionTick());

            if (! state.clipSelection().empty() && ! pianoRoll.hasKeyboardFocus (true))
            {
                const auto step = bar ? map.timeSignatureAtTick (pos).ticksPerBar() : std::max<collab::Tick> (1, state.grid.stepTicks());
                ctx.nudgeClips (state.clipSelection(), dir * step);
            }
            else if (bar)
            {
                const int b = map.tickToBar (pos);
                const bool onBar = map.barToTick (b) == pos;
                bridge.setPositionTick ((double) map.barToTick (juce::jmax (1, dir > 0 ? b + 1 : (onBar ? b - 1 : b))));
            }
            else
            {
                const auto step = std::max<collab::Tick> (1, state.grid.stepTicks());
                const auto snapped = (pos / step) * step;
                bridge.setPositionTick ((double) juce::jmax<collab::Tick> (0, dir > 0 ? snapped + step : (snapped == pos ? pos - step : snapped)));
            }

            break;
        }
        case cmdPianoFull:     togglePianoFullScreen(); break;
        case cmdTrackUp: case cmdTrackDown:
        {
            const auto& tracks = document.getProject().tracks;

            if (tracks.empty())
                break;

            int index = document.getProject().indexOfTrack (state.selectedTrackId);
            const int step = info.commandID == cmdTrackUp ? -1 : 1;
            index = index < 0 ? 0 : juce::jlimit (0, (int) tracks.size() - 1, index + step);

            // 表示していないトラック（バウンスしたもの）は飛ばす
            while (state.isHidden (tracks[(size_t) index].id) && index + step >= 0 && index + step < (int) tracks.size())
                index += step;

            if (state.isHidden (tracks[(size_t) index].id))
                break;

            state.selectedTrackId = tracks[(size_t) index].id;
            state.selectClip ({});
            state.changed();
            break;
        }
        case cmdToLoopEnd:     bridge.setPositionTick ((double) state.loopEnd); break;
        case cmdForward:
        case cmdRewind:
        {
            const auto& map = document.getTempoMap();
            const int bar = map.tickToBar ((collab::Tick) std::llround (bridge.getPositionTick()));
            const auto barStart = map.barToTick (bar);
            const bool onBar = std::llabs ((collab::Tick) std::llround (bridge.getPositionTick()) - barStart) < 5;
            const int target = info.commandID == cmdForward ? bar + 1 : (onBar ? juce::jmax (1, bar - 1) : bar);
            bridge.setPositionTick ((double) map.barToTick (target));
            break;
        }
        case cmdShortcuts:     showShortcuts(); break;
        case cmdModeCubase:    setOperationMode (OperationMode::cubase); break;
        case cmdModeStudioOne: setOperationMode (OperationMode::studioOne); break;
        case cmdMixer:         toggleMixer(); break;
        case cmdMaster:        openMaster(); break;
        case cmdLoopToSelection: loopToSelection(); break;
        case cmdPlugins:       showPluginManager(); break;
        case cmdImportAudio:   importAudio(); break;
        case cmdImportMidi:    importMidi(); break;
        case cmdExportMixdown: showExportPanel(); break;
        case cmdAudioFiles:    showAudioFiles(); break;
        case cmdSplit:         ctx.splitAtPlayhead(); break;
        case cmdMuteTrack:
        case cmdSoloTrack:
            if (auto* t = ctx.selectedTrack())
            {
                const bool mute = info.commandID == cmdMuteTrack;
                auto id = t->id;
                document.perform (mute ? "ミュート"_ju : "ソロ"_ju, [id, mute] (collab::Project& p)
                {
                    if (auto* tr = p.findTrack (id))
                        (mute ? tr->mute : tr->solo) = ! (mute ? tr->mute : tr->solo);
                });
            }
            break;
        case cmdArmTrack:
            if (auto* t = ctx.selectedTrack())
                ctx.toggleRecordArm (t->id);
            break;
        case cmdStretchSong:
            showStretchSongDialog();
            break;
        case cmdAutoArm:
            state.autoArmSelected = ! state.autoArmSelected;
            settings.setValue ("autoArmSelected", state.autoArmSelected);

            // 切ったときは、自動で待機にしていたトラックを戻す
            if (! state.autoArmSelected && ! autoArmedTrackId.empty())
                ctx.setRecordArm (std::exchange (autoArmedTrackId, std::string()), false, false);

            autoArmedFor.clear();
            followSelectionWithRecordArm();
            commandManager.commandStatusChanged();
            break;
        case cmdTrackHeight:
            if (auto* t = ctx.selectedTrack())
            {
                const bool isMax = state.clipLaneHeight (t->id) >= EditorState::maxTrackHeight;
                state.trackHeights[t->id] = isMax ? EditorState::minTrackHeight : EditorState::maxTrackHeight;
                state.changed();
            }
            break;
        case cmdAudioSettings: showAudioSettings(); break;
        case cmdSyncSettings:  showServerSettings(); break;
        case cmdSyncRegister:  registerProject(); break;
        case cmdSyncOpen:      showProjectPicker(); break;
        case cmdSyncPull:      downloadWithChoices ({}, false); break;
        case cmdSyncPush:      if (syncPanel.isCollapsed()) toggleSyncPanel(); break;
        case cmdSyncHistory:   showHistory(); break;
        case cmdSyncPanel:     toggleSyncPanel(); break;
        case cmdInspector:
            inspector.setVisible (! inspector.isVisible());
            settings.setValue ("inspectorVisible", inspector.isVisible());
            resized();
            commandManager.commandStatusChanged();
            break;
        case cmdSyncCreate:    createProjectOnServer(); break;
        case cmdCredits:    showCredits(); break;
        case cmdAbout:
            Dialogs::showInfo ("ShareDAW について"_ju,
                               "ShareDAW "_ju + (Updater::currentBuild() > 0 ? Updater::versionText (Updater::currentBuild())
                                                                              : "（開発版）"_ju)
                                 + "\n仲間と曲を作るための DAW"_ju);
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
    return { "ファイル"_ju, "編集"_ju, "トランスポート"_ju, "トラック"_ju, "オーディオ"_ju, "同期"_ju, "表示"_ju, "設定"_ju, "ヘルプ"_ju };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    auto* cm = &commandManager;

    switch (index)
    {
        case 0:
            m.addCommandItem (cm, cmdNew);
            m.addCommandItem (cm, cmdSyncOpen);
            m.addCommandItem (cm, cmdOpen);
            m.addCommandItem (cm, cmdSave);
            m.addSeparator();
            m.addCommandItem (cm, cmdImportAudio);
            m.addCommandItem (cm, cmdImportMidi);
            m.addCommandItem (cm, cmdExportMixdown);
           #if ! JUCE_MAC
            m.addSeparator();
            m.addCommandItem (cm, juce::StandardApplicationCommandIDs::quit);
           #endif
            break;
        case 1:
            m.addCommandItem (cm, cmdUndo);
            m.addCommandItem (cm, cmdRedo);
            m.addSeparator();
            m.addCommandItem (cm, cmdCut);
            m.addCommandItem (cm, cmdCopy);
            m.addCommandItem (cm, cmdPaste);
            m.addCommandItem (cm, cmdDelete);
            m.addCommandItem (cm, cmdSelectAll);
            m.addCommandItem (cm, cmdDuplicate);
            m.addCommandItem (cm, cmdNudgeLeft);
            m.addCommandItem (cm, cmdNudgeRight);
            m.addCommandItem (cm, cmdSplit);
            m.addCommandItem (cm, cmdQuantise);
            {
                // 選んだノート（ピアノロール）があればノート、なければ選んだ MIDI クリップ
                const bool notes = pianoRoll.hasSelectedNotes();
                std::set<std::string> clips;

                for (auto& id : state.clipSelection())
                    for (auto& t : document.getProject().tracks)
                        if (t.findMidiClip (id) != nullptr)
                            clips.insert (id);

                auto stretch = AppContext::stretchMenu ([this, notes, clips] (double f)
                {
                    if (notes)
                        pianoRoll.stretchSelection (f);
                    else
                        ctx.stretchMidiClips (clips, f);
                });
                m.addSubMenu (notes ? "選んだノートの伸び縮み"_ju : "選んだ MIDI クリップの伸び縮み"_ju, stretch, notes || ! clips.empty());
            }
            m.addCommandItem (cm, cmdStretchSong);
            m.addSeparator();
            m.addCommandItem (cm, cmdToolSelect);
            m.addCommandItem (cm, cmdToolPencil);
            m.addCommandItem (cm, cmdToolSplit);
            m.addCommandItem (cm, cmdSnap);
            break;
        case 2:
            m.addCommandItem (cm, cmdPlay);
            m.addCommandItem (cm, cmdStop);
            m.addCommandItem (cm, cmdRecord);
            m.addCommandItem (cm, cmdAutoArm);
            m.addCommandItem (cm, cmdToStart);
            m.addCommandItem (cm, cmdToLoopStart);
            m.addCommandItem (cm, cmdToLoopEnd);
            m.addCommandItem (cm, cmdLoop);
            m.addCommandItem (cm, cmdLoopToSelection);
            m.addSeparator();
            m.addCommandItem (cm, cmdAddMarker);

            {
                juce::PopupMenu markers;

                for (int c = cmdMarker1; c <= cmdMarker9; ++c)
                    markers.addCommandItem (cm, c);

                m.addSubMenu ("マーカーへ移動"_ju, markers);
            }
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
            m.addCommandItem (cm, cmdImportAudio);
            m.addCommandItem (cm, cmdAudioFiles);
            break;
        case 5:
        {
            m.addCommandItem (cm, cmdSyncPanel);
            m.addSeparator();
            m.addCommandItem (cm, cmdSyncPull);
            m.addCommandItem (cm, cmdSyncPush);
            m.addSeparator();
            m.addCommandItem (cm, cmdSyncHistory);

            m.addSeparator();
            m.addCommandItem (cm, cmdSyncCreate);
            m.addCommandItem (cm, cmdSyncRegister);
            m.addCommandItem (cm, cmdSyncOpen);
            m.addCommandItem (cm, cmdSyncSettings);
            break;
        }
        case 6:
        {
            juce::PopupMenu sizes;

            for (int c = cmdFont100; c <= cmdFont200; ++c)
                sizes.addCommandItem (cm, c);

            m.addCommandItem (cm, cmdMixer);
            m.addCommandItem (cm, cmdMaster);
            m.addCommandItem (cm, cmdInspector);
            m.addCommandItem (cm, cmdPianoFull);
            m.addSeparator();
            m.addCommandItem (cm, cmdZoomIn);
            m.addCommandItem (cm, cmdZoomOut);
            m.addCommandItem (cm, cmdWaveBigger);
            m.addCommandItem (cm, cmdWaveSmaller);
            m.addCommandItem (cm, cmdTransientsAtPeak);
            m.addCommandItem (cm, cmdAutoScroll);
            m.addSeparator();
            m.addSubMenu ("文字サイズ（画面共有用）"_ju, sizes);
            break;
        }
        case 7:
        {
            // 設定: オーディオ（MIDI 入力もここ）・プラグイン・サーバー・操作モード
            m.addCommandItem (cm, cmdAudioSettings);
            m.addCommandItem (cm, cmdPlugins);
            m.addCommandItem (cm, cmdSyncSettings);
            m.addSeparator();

            juce::PopupMenu modes;
            modes.addCommandItem (cm, cmdModeCubase);
            modes.addCommandItem (cm, cmdModeStudioOne);
            m.addSubMenu ("操作モード"_ju, modes);

            // 外観（色）: ダーク / ライト。部品が作るときに色を読むので、再起動で切り替わる
            juce::PopupMenu looks;
            auto chooseLook = [this] (bool useLight)
            {
                if (useLight == Theme::light)
                    return;

                settings.setValue ("uiTheme", useLight ? "light" : "dark");
                settings.saveIfNeeded();

                // その場で画面を作り直して切り替える（曲はそのまま。選択やズームは初めに戻る）
                if (onAppearanceChanged)
                    onAppearanceChanged (useLight);
            };
            const bool savedLight = Theme::light;
            looks.addItem ("ダーク"_ju, true, ! savedLight, [chooseLook] { chooseLook (false); });
            looks.addItem ("ライト"_ju, true, savedLight, [chooseLook] { chooseLook (true); });
            m.addSubMenu ("外観"_ju, looks);
            break;
        }
        case 8:
            m.addCommandItem (cm, cmdCredits);
            m.addCommandItem (cm, cmdShortcuts);
            m.addCommandItem (cm, cmdCheckUpdate);
            m.addCommandItem (cm, cmdAbout);
            break;
        default: break;
    }

    return m;
}
