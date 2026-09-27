#pragma once

#include <map>

#include "Common.h"
#include "InstrumentLibrary.h"
#include "ProjectDocument.h"

class SfizzPlugin;
class ChannelStripPlugin;
class CountInPlugin;

/**
    プロジェクト JSON（collab::Project）→ Tracktion Edit の変換層（§2.1）。

    - Edit は実行時の表現にすぎず、保存しない。ProjectDocument が変わるたびに差分を反映する。
    - 時間の扱い: Edit のテンポは 60BPM・4/4 に固定し「1拍 = 1秒」とする。
      tick → 秒の変換は collab::TempoMap（四分音符基準の BPM・拍子の分母を正しく扱う）で行い、
      その秒数をそのまま Tracktion の拍数として置く。これにより Tracktion 側の拍子・テンポの解釈に依存しない。
    - Edit 上の編集 → JSON への反映（録音結果の取り込みなど）は M2 で追加する。
*/
class EngineBridge  : private juce::ChangeListener,
                      private te::TransportControl::Listener
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

    /** トラック1本をバウンスする（§3.7: 48kHz / 32bit float、先頭から末尾 + 余白）。 */
    juce::Result renderTrack (const std::string& trackId, const juce::File& output, double tailSeconds = 2.0);

    /** バウンスして audio/<hash>.wav に保存し、Project に書き込む render 情報を返す（プロジェクトの保存先が必要）。
        先に flushPluginStates() を呼んでおくこと（フィンガープリントに状態ファイルのハッシュが入る）。 */
    juce::Result bounceTrack (const std::string& trackId, collab::Render& result);

    /** トラックの音の元（MIDI・音源・エフェクト・プラグインの状態）のフィンガープリント（§3.7）。 */
    std::string trackFingerprint (const collab::Track&) const;

    /** 外部プラグインの状態を plugins-state/ に書き出す（保存・バウンス・push の前に呼ぶ）。変わったら true。 */
    bool flushPluginStates();

    /** 外部プラグイン（音源は effectId を空に）。エディタを開くため。 */
    te::Plugin* getExternalPlugin (const std::string& trackId, const std::string& effectId = {}) const;

    /** このトラックをバウンスした音で再生しているか（プラグインを鳴らせない環境）。 */
    bool isPlayingRender (const std::string& trackId) const;

    te::Engine& getEngine() noexcept                { return engine; }

    /** ノートを短く鳴らす（ピアノロールでクリック・入力したときの確認用）。 */
    void previewNote (const std::string& trackId, int pitch, int velocity);

    /** 前回呼んでからのトラックのピーク（dB、左右の大きいほう）。ミキサーのメーター用。trackId が空ならコードトラック。 */
    float getTrackPeakDb (const std::string& trackId);

    /** メトロノーム・マスターのピーク（dB）。マスターはマスター音量をかけた後の値。 */
    float getMetronomePeakDb();
    float getMasterPeakDb();

    /** マスター音量（この PC だけの設定。書き出しにもかかる）。 */
    void setMasterVolumeDb (float db);
    float getMasterVolumeDb() const noexcept               { return masterVolumeDb; }

    /** トラックのコンプのゲインリダクション（dB、0 以上）。 */
    float getTrackGainReductionDb (const std::string& trackId) const;

    //==============================================================================
    // 録音（§3.5）。入力の割り当て・録音待機・モニタリングはこの環境だけの設定なので JSON には入れない。
    struct TrackInput
    {
        juce::String device;     // 入力デバイス名（空なら未割り当て）
        bool armed = false;      // 録音待機
        bool monitor = false;    // ソフトウェアモニタリング
    };

    /** 使える入力（モノラルの入力チャンネルごと）。 */
    juce::StringArray getAudioInputs() const;

    TrackInput getTrackInput (const std::string& trackId) const;
    void setTrackInput (const std::string& trackId, const TrackInput&);

    /** 録音を始める。停止中なら再生位置から countInBars 小節のカウントインのあとに録音する。 */
    juce::Result startRecording (int countInBars);
    bool isRecording() const;

    /** 手動のレイテンシ補正（サンプル）。ドライバが報告するレイテンシの補正に加えてずらす。 */
    void setManualLatencySamples (int samples);

    struct RecordedTake
    {
        std::string trackId;
        juce::File file;
        double startSeconds = 0, offsetSeconds = 0, lengthSeconds = 0;
        double punchInSeconds = 0;   // 録音を始めた位置（これより前はカウントイン）
    };

    /** 録音が終わったとき（メッセージスレッド）。受け取った側で audio/ に取り込み、元のファイルを消す。 */
    std::function<void (std::vector<RecordedTake>)> onRecordingFinished;

    /** プラグインを削除する直前に呼ばれる（エディタのウィンドウを閉じるため）。 */
    std::function<void (te::Plugin*)> onPluginRemoved;

    /** 今すぐ Project を Edit に反映する。 */
    void sync();

private:
    /** トラックの音量メーター（ミキサー用）。トラックを消す前に外す。 */
    struct Meter
    {
        te::LevelMeasurer* measurer = nullptr;
        te::LevelMeasurer::Client client;

        void attach (te::AudioTrack&);
        void attach (te::LevelMeasurer&);
        void detach();
        ~Meter()    { detach(); }
    };

    struct Binding
    {
        te::AudioTrack::Ptr track;
        std::unique_ptr<Meter> meter;
        SfizzPlugin* synth = nullptr;
        ChannelStripPlugin* strip = nullptr;
        juce::String sfzText;
        std::string clipsKey;
        juce::String problem;
        int missingAudio = 0;

        bool renderMode = false;
        te::Plugin::Ptr externalInstrument;
        std::string instrumentKey, instrumentStateRef;

        struct Effect { std::string id, stateRef; te::Plugin::Ptr plugin; };
        std::vector<Effect> effects;
        std::string effectsKey;
    };

    te::Engine& engine;
    ProjectDocument& document;
    const InstrumentLibrary& library;
    std::unique_ptr<te::Edit> edit;

    std::map<std::string, Binding> bindings;
    std::string tempoKey;

    te::AudioTrack::Ptr chordTrack;
    std::unique_ptr<Meter> chordMeter, metronomeMeter, masterMeter;
    float masterVolumeDb = 0.0f;
    SfizzPlugin* chordSynth = nullptr;
    juce::String chordSfzText;
    std::string chordKey;

    te::AudioTrack::Ptr metronomeTrack;
    std::string metronomeKey;
    bool metronomeEnabled = false;
    float metronomeVolumeDb = -6.0f;

    std::map<std::string, TrackInput> trackInputs;
    CountInPlugin* countIn = nullptr;
    std::vector<RecordedTake> pendingTakes;
    int manualLatencySamples = 0;
    double punchInSeconds = 0;
    std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true);

    bool loopEnabled = false;
    collab::Tick loopStart = 0, loopEnd = 0;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    te::AudioTrack::Ptr createTrack();
    SfizzPlugin* addSynth (te::AudioTrack&);
    void syncTrack (const collab::Track&, Binding&, bool tempoChanged);
    void syncInstrument (const collab::Track&, Binding&);
    void syncEffects (const collab::Track&, Binding&);
    void syncStrip (const collab::Track&, Binding&);
    void removeInstrument (Binding&);
    void removeEffects (Binding&);
    bool canPlayLive (const collab::Track&, juce::String& why) const;
    te::Plugin::Ptr createExternal (const collab::ExternalPlugin&, const std::string& stateRef);
    void syncMetronome (bool tempoChanged);
    void syncChordTrack (bool tempoChanged);
    void applyLoop();
    void applyInputs();
    void configureInputs();
    void restoreAfterRecording();

    void recordingStopped (te::SyncPoint, bool discardRecordings) override;
    void recordingFinished (te::InputDeviceInstance&, te::EditItemID targetID,
                            const juce::ReferenceCountedArray<te::Clip>& recordedClips) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineBridge)
};
