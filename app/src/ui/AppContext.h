#pragma once

#include <optional>
#include <set>

#include "EngineBridge.h"
#include "collab/BuiltinEffects.h"
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


    /** 新しい MIDI トラック（内蔵音源）を追加して選択する。 */
    void addBuiltinMidiTrack (const std::string& instrumentId, const juce::String& name);

    /** 空のオーディオトラックを追加して選択する。 */
    std::string addAudioTrack (const juce::String& name);

    //==============================================================================
    // バス（Cubase のグループ・FX チャンネル）
    /** バストラックを追加する（選択中のトラックの下）。ID を返す。 */
    std::string addBusTrack (const juce::String& name);
    /** トラック 1 本を変更する（元に戻せる）。トラックが消えていれば何もしない。mergeId が同じ操作は 1 回の「元に戻す」にまとめる。 */
    void editTrack (const std::string& trackId, const juce::String& description, std::function<void (collab::Track&)> fn,
                    const juce::String& mergeId = {});

    void setTrackOutput (const std::string& trackId, const std::string& busId);
    void setSend (const std::string& trackId, const std::string& busId, std::optional<double> levelDb,
                  std::optional<bool> preFader, const juce::String& mergeId = {});
    void removeSend (const std::string& trackId, const std::string& busId);

    /** 出力先とセンドのメニュー（トラックヘッダー・ミキサー共通）。 */
    juce::PopupMenu routingMenu (const std::string& trackId);
    juce::PopupMenu outputMenu (const std::string& trackId);
    juce::PopupMenu sendMenu (const std::string& trackId);

    /** エフェクト（外部プラグイン）を追加するメニュー。 */
    juce::PopupMenu addEffectMenu (const std::string& trackId);
    juce::String outputName (const collab::Track&) const;

    /**
        オーディオファイルを読み込んでクリップを作る（§3.6: 48kHz / 32bit float WAV に変換）。
        trackId が空ならオーディオトラックを新しく作る。複数のファイルは順に並べる。
    */
    void importAudioFiles (const juce::Array<juce::File>&, std::string trackId, collab::Tick atTick);

    /**
        MIDI ファイルを読み込んでクリップを作る。trackId が MIDI トラックで、ファイルのパートが 1 つならそのトラックに置く。
        それ以外はパートごとに新しいトラックを作る（音源はチャンネル・プログラムから選ぶ）。
        プロジェクトにまだクリップがなければ、ファイルのテンポと拍子も取り込む。
    */
    void importMidiFiles (const juce::Array<juce::File>&, std::string trackId, collab::Tick atTick);

    /** 選択中のトラックで、再生位置にあるクリップを分割する。 */
    void splitAtPlayhead();

    //==============================================================================
    // クリップの編集（Cubase のプロジェクトウィンドウの操作）
    struct ClipRef { std::string trackId; bool audio = false; };
    std::optional<ClipRef> findClip (const std::string& clipId) const;

    /** クリップを at で分割する（はさみツール）。 */
    void splitClipAt (const std::string& clipId, collab::Tick at);


    void deleteClips (const std::set<std::string>& clipIds);
    void duplicateClips (const std::set<std::string>& clipIds);
    void copyClips (const std::set<std::string>& clipIds);
    /** 再生位置に貼り付ける（元のトラック、なければ選択中の同じ種類のトラックへ）。 */
    void pasteClips (collab::Tick at);
    bool hasClipsInClipboard() const noexcept       { return ! clipboard.midi.empty() || ! clipboard.audio.empty(); }
    void nudgeClips (const std::set<std::string>& clipIds, collab::Tick delta);

    /** クリップの範囲（開始・終わり）。 */
    std::pair<collab::Tick, collab::Tick> clipRange (const std::string& clipId) const;

    struct ClipClipboard
    {
        std::vector<std::pair<std::string, collab::MidiClip>> midi;    // (元のトラック, クリップ)
        std::vector<std::pair<std::string, collab::AudioClip>> audio;
        collab::Tick origin = 0;                                         // いちばん左の開始位置
    };

    ClipClipboard clipboard;

    // 外部プラグイン（§3.4）とバウンス（§3.7）
    void setBuiltinInstrument (const std::string& trackId, const std::string& instrumentId);
    void setExternalInstrument (const std::string& trackId, const juce::PluginDescription&);
    void addEffect (const std::string& trackId, const juce::PluginDescription&);
    void addBuiltinEffect (const std::string& trackId, collab::fx::Type);

    /** インサートに出す名前（内蔵エフェクトは種類の名前）。 */
    static juce::String effectName (const collab::Effect&);
    void removeEffect (const std::string& trackId, const std::string& effectId);
    void toggleEffectBypass (const std::string& trackId, const std::string& effectId);
    void bounceTrack (const std::string& trackId);

    /** 現在の内容のフィンガープリント（plugins-state の内容を含む）。 */
    std::string fingerprint (const collab::Track&) const;

    /** プラグインのエディタを開く（MainComponent が設定する）。 */
    std::function<void (const std::string& trackId, const std::string& effectId)> openPluginEditor;

    /**
        トラックの録音待機（●）を切り替える（R キーとトラックヘッダーのボタン）。
        オーディオトラックは入力がなければ、モノなら 1 つ、ステレオなら隣り合う 2 つの入力を割り当てる。
        MIDI トラックは MIDI キーボードの録音先にする（1 つだけ）。
    */
    void toggleRecordArm (const std::string& trackId);

    /** オーディオトラックの入力の選択肢（モノ: 入力ごと、ステレオ: 隣り合う 2 つ）。表示名と (左, 右)。 */
    struct InputChoice { juce::String label, left, right; };
    std::vector<InputChoice> inputChoices (const std::string& trackId) const;

    /** 録音の開始・停止（MainComponent が設定する）。 */
    std::function<void()> toggleRecord;

    /** 「トラックを追加」のメニュー（オーディオ / 音源 → ドラム・ベース・ピアノ）。MainComponent が設定する。 */
    std::function<juce::PopupMenu()> addTrackMenu;

    /** トラックの EQ・コンプの画面を開く（MainComponent が設定する）。 */
    std::function<void (const std::string& trackId, bool compressor)> openChannelStrip;   // EQ か Compressor の画面

    /** マスターの画面（リミッター・ラウドネス）を開く。 */
    std::function<void()> openMaster;
};
