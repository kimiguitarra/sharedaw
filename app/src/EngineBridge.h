#pragma once

#include <map>

#include "Common.h"
#include "InstrumentLibrary.h"
#include "ProjectDocument.h"

class SfizzPlugin;

/**
    プロジェクト JSON（collab::Project）→ Tracktion Edit の変換層（§2.1）。

    - Edit は実行時の表現にすぎず、保存しない。ProjectDocument が変わるたびに差分を反映する。
    - 時間の扱い: Edit のテンポは 60BPM・4/4 に固定し「1拍 = 1秒」とする。
      tick → 秒の変換は collab::TempoMap（四分音符基準の BPM・拍子の分母を正しく扱う）で行い、
      その秒数をそのまま Tracktion の拍数として置く。これにより Tracktion 側の拍子・テンポの解釈に依存しない。
    - Edit 上の編集 → JSON への反映（録音結果の取り込みなど）は M2 で追加する。
*/
class EngineBridge  : private juce::ChangeListener
{
public:
    EngineBridge (te::Engine&, ProjectDocument&, const InstrumentLibrary&);
    ~EngineBridge() override;

    te::Edit& getEdit() noexcept                    { return *edit; }

    //==============================================================================
    void play();
    void stop();
    void togglePlay();
    bool isPlaying() const;
    void returnToStart();

    double getPositionSeconds() const;
    double getPositionTick() const;
    void setPositionTick (double tick);

    void setLoop (bool enabled, collab::Tick start, collab::Tick end);
    bool isLooping() const;

    void setMetronome (bool enabled, float volumeDb);
    bool isMetronomeEnabled() const noexcept        { return metronomeEnabled; }

    /** トラックの音源が読み込めていない場合の理由（問題なければ空）。 */
    juce::String getInstrumentProblem (const std::string& trackId) const;

    /** 先頭から endTick + tailSeconds までをオフラインで WAV に書き出す（メトロノームは含めない）。 */
    bool renderToFile (const juce::File& output, collab::Tick endTick, double tailSeconds = 2.0);

    /** 今すぐ Project を Edit に反映する。 */
    void sync();

private:
    struct Binding
    {
        te::AudioTrack::Ptr track;
        SfizzPlugin* synth = nullptr;
        juce::String sfzText;
        std::string clipsKey;
        juce::String problem;
        int missingAudio = 0;
    };

    te::Engine& engine;
    ProjectDocument& document;
    const InstrumentLibrary& library;
    std::unique_ptr<te::Edit> edit;

    std::map<std::string, Binding> bindings;
    std::string tempoKey;

    te::AudioTrack::Ptr chordTrack;
    SfizzPlugin* chordSynth = nullptr;
    juce::String chordSfzText;
    std::string chordKey;

    te::AudioTrack::Ptr metronomeTrack;
    std::string metronomeKey;
    bool metronomeEnabled = false;
    float metronomeVolumeDb = -6.0f;

    bool loopEnabled = false;
    collab::Tick loopStart = 0, loopEnd = 0;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    te::AudioTrack::Ptr createTrack();
    SfizzPlugin* addSynth (te::AudioTrack&);
    void syncTrack (const collab::Track&, Binding&, bool tempoChanged);
    void syncInstrument (const collab::Track&, Binding&);
    void syncMetronome (bool tempoChanged);
    void syncChordTrack (bool tempoChanged);
    void applyLoop();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineBridge)
};
