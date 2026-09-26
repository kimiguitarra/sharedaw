#pragma once

#include "EngineBridge.h"
#include "InstrumentLibrary.h"
#include "ProjectDocument.h"
#include "ui/EditorState.h"

class SyncManager;
class AudioFileCache;

/** UI の各部品が共有する参照。 */
struct AppContext
{
    ProjectDocument& document;
    EditorState& state;
    EngineBridge& engine;
    const InstrumentLibrary& library;
    SyncManager& sync;
    AudioFileCache& audioCache;

    /** 選択中のトラック・クリップ（無ければ nullptr）。 */
    const collab::Track* selectedTrack() const      { return document.getProject().findTrack (state.selectedTrackId); }

    const collab::MidiClip* selectedClip() const
    {
        auto* t = selectedTrack();
        return t != nullptr ? t->findMidiClip (state.selectedClipId) : nullptr;
    }

    /** ロック操作のメニュー項目を追加する（同期中のみ。MainComponent が設定する）。 */
    std::function<void (const std::string& scopeId, juce::PopupMenu&)> addLockMenuItems;

    /** 新しい MIDI トラック（内蔵音源）を追加して選択する。 */
    void addBuiltinMidiTrack (const std::string& instrumentId, const juce::String& name);

    /** 空のオーディオトラックを追加して選択する。 */
    std::string addAudioTrack (const juce::String& name);

    /**
        オーディオファイルを読み込んでクリップを作る（§3.6: 48kHz / 32bit float WAV に変換）。
        trackId が空ならオーディオトラックを新しく作る。複数のファイルは順に並べる。
    */
    void importAudioFiles (const juce::Array<juce::File>&, std::string trackId, collab::Tick atTick);

    /** 選択中のトラックで、再生位置にあるクリップを分割する。 */
    void splitAtPlayhead();

    // 外部プラグイン（§3.4）とバウンス（§3.7）
    void setBuiltinInstrument (const std::string& trackId, const std::string& instrumentId);
    void setExternalInstrument (const std::string& trackId, const juce::PluginDescription&);
    void addEffect (const std::string& trackId, const juce::PluginDescription&);
    void removeEffect (const std::string& trackId, const std::string& effectId);
    void toggleEffectBypass (const std::string& trackId, const std::string& effectId);
    void bounceTrack (const std::string& trackId);

    /** 現在の内容のフィンガープリント（plugins-state の内容を含む）。 */
    std::string fingerprint (const collab::Track&) const;

    /** プラグインのエディタを開く（MainComponent が設定する）。 */
    std::function<void (const std::string& trackId, const std::string& effectId)> openPluginEditor;
};
