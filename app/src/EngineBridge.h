#pragma once

#include "collab/MasterDsp.h"

#include <limits>
#include <map>

#include "Common.h"
#include "InstrumentLibrary.h"
#include "ProjectDocument.h"
#include "plugins/HostSyncedPlugin.h"

class SfizzPlugin;
class ChannelStripPlugin;
class CountInPlugin;

/**
    プロジェクト JSON（collab::Project）→ Tracktion Edit の変換層（§2.1）。

    - Edit は実行時の表現にすぎず、保存しない。ProjectDocument が変わるたびに差分を反映する。
    - 時間の扱い: Edit のテンポは 60BPM・4/4 に固定し「1拍 = 1秒」とする。
      tick → 秒の変換は collab::TempoMap（四分音符基準の BPM・拍子の分母を正しく扱う）で行い、
      その秒数をそのまま Tracktion の拍数として置く。これにより Tracktion 側の拍子・テンポの解釈に依存しない。
      外部プラグインには HostSyncedExternalPlugin が曲のテンポ・拍子・PPQ を渡す（Follow Host）。
    - Edit 上の編集 → JSON への反映（録音結果の取り込みなど）は M2 で追加する。
*/
class EngineBridge  : private juce::ChangeListener,
                      private te::TransportControl::Listener,
                      private te::MidiInputDevice::MidiKeyChangeDispatcher::Listener
{
public:
    EngineBridge (te::Engine&, ProjectDocument&, const InstrumentLibrary&);
    ~EngineBridge() override;


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

    void setMetronome (bool enabled, float volumeDb);

    /** トラックの音源が読み込めていない場合の理由（問題なければ空）。 */
    juce::String getInstrumentProblem (const std::string& trackId) const;

    /** 先頭から endTick + tailSeconds までをオフラインで WAV に書き出す（メトロノームは含めない）。sampleRate は書き出すレート。 */
    bool renderToFile (const juce::File& output, collab::Tick endTick, double tailSeconds = 2.0, int bitDepth = 32,
                       double sampleRate = (double) collab::kSampleRate);

    /** トラック1本をバウンスする（§3.7: 48kHz / 32bit float、先頭から末尾 + 余白）。 */
    juce::Result renderTrack (const std::string& trackId, const juce::File& output, double tailSeconds = 2.0);

    /**
        パラデータ（トラックごとの書き出し）の 1 本。ミックスで聞こえるとおり（インサート・EQ・Comp・音量・パンを含む）で、
        センド・バス・マスターは通さない。trackId が空ならコードトラック。先頭から endSeconds まで（全部のファイルの長さをそろえる）。
    */
    juce::Result renderStem (const std::string& trackId, const juce::File& output, double endSeconds, int bitDepth = 24);

    /** バウンスして audio/<hash>.wav に保存し、Project に書き込む render 情報を返す（プロジェクトの保存先が必要）。
        先に flushPluginStates() を呼んでおくこと（フィンガープリントに状態ファイルのハッシュが入る）。 */
    juce::Result bounceTrack (const std::string& trackId, collab::Render& result, collab::SampleCount& lengthSamples);

    /** トラックの音の元（MIDI・音源・エフェクト・プラグインの状態）のフィンガープリント（§3.7）。 */
    std::string trackFingerprint (const collab::Track&) const;

    /** 外部プラグインの状態を plugins-state/ に書き出す（保存・バウンス・push の前に呼ぶ）。変わったら true。 */
    bool flushPluginStates();

    /** 外部プラグイン（音源は effectId を空に）。エディタを開くため。 */
    te::Plugin* getExternalPlugin (const std::string& trackId, const std::string& effectId = {}) const;

    /** 内蔵エフェクト（バスコンプ）のゲインリダクション（dB）。画面のメーター用。 */
    float getEffectGainReductionDb (const std::string& trackId, const std::string& effectId) const;

    /**
        書き出しの後ろに足す余韻（秒）。trackId が空なら全トラック（とその送り先のバス）の最大。
        内蔵リバーブは DECAY から、外部プラグインは報告する長さから。最低 minimum 秒。
    */
    double tailSecondsFor (const std::string& trackId, double minimum = 2.0) const;

    /** このトラックをバウンスした音で再生しているか（プラグインを鳴らせない環境）。 */
    bool isPlayingRender (const std::string& trackId) const;

    te::Engine& getEngine() noexcept                { return engine; }

    /** ノートを短く鳴らす（ピアノロールでクリック・入力したときの確認用）。 */
    void previewNote (const std::string& trackId, int pitch, int velocity);

    /** 左右のピーク（dB）。 */
    struct StereoPeak { float left = -100.0f, right = -100.0f; };

    /** 前回呼んでからのトラックのピーク。ミキサーのメーター用。trackId が空ならコードトラック。 */
    StereoPeak getTrackPeakDb (const std::string& trackId);

    /** 動作確認用: エンジン（Edit）のトラックの MIDI クリップにあるノートの数。プロジェクトのノートと同じはず。 */
    int countEngineNotes (const std::string& trackId) const;
    int countEngineClips (const std::string& trackId) const;

    /** メトロノーム・マスターのピーク。マスターはマスター音量をかけた後の値。 */
    StereoPeak getMetronomePeakDb();
    StereoPeak getMasterPeakDb();

    /** マスター音量（この PC だけの設定。書き出しにもかかる）。 */
    void setMasterVolumeDb (float db);
    float getMasterVolumeDb() const noexcept               { return masterVolumeDb; }

    /** スペクトラム表示（EQ 画面）。有効にしたトラックの EQ・コンプ後の音を読み出す。 */
    void setSpectrumTrack (const std::string& trackId);
    bool getSpectrumSamples (float* dest, int numSamples, double& sampleRate) const;

    /** マスターのリミッターとラウドネス（LUFS）。 */
    struct MasterStatus
    {
        float gainReductionDb = 0.0f, inputPeakDb = -100.0f, outputPeakDb = -100.0f;
        double momentaryLufs = -100.0, shortTermLufs = -100.0, integratedLufs = -100.0, seconds = 0.0;
    };

    /** 定期的に呼ぶ（ラウドネスの集計を進める）。再生を始めたときにインテグレーテッドをリセットする。 */
    MasterStatus pollMaster();
    void resetLoudness()                                    { loudnessStats.reset(); }

    /** トラックのコンプのゲインリダクション（dB、0 以上）。 */
    float getTrackGainReductionDb (const std::string& trackId) const;

    //==============================================================================
    // 録音（§3.5）。入力の割り当て・録音待機・モニタリングはこの環境だけの設定なので JSON には入れない。
    struct TrackInput
    {
        juce::String device;     // 入力デバイス名（空なら未割り当て）。ステレオのトラックでは左
        juce::String deviceRight;// ステレオのトラックの右の入力（モノのトラックでは空）
        bool armed = false;      // 録音待機
        bool monitor = false;    // ソフトウェアモニタリング
    };

    /** オーディオ入力（モノラルのチャンネルごと）の前回呼んでからのピーク（dB）。ミキサーの入力ストリップ用。 */
    struct InputLevel { juce::String name; float peakDb = -100.0f; };
    std::vector<InputLevel> getInputLevels();

    /** 使える入力（モノラルの入力チャンネルごと）。 */
    juce::StringArray getAudioInputs() const;

    TrackInput getTrackInput (const std::string& trackId) const;
    void setTrackInput (const std::string& trackId, const TrackInput&);

    /** 録音を始める。停止中なら再生位置から countInBars 小節のカウントインのあとに録音する。 */
    juce::Result startRecording (int countInBars);
    bool isRecording() const;

    /** 録音中に入ってきている音（画面に出すため。録音が終わると消え、取り込んだクリップに替わる）。 */
    struct LiveRecording
    {
        double startSeconds = 0.0;                      // 録音を始めた位置（秒）
        std::vector<std::pair<double, float>> peaks;    // オーディオ: (秒, その間のピーク 0〜1)
        struct Note { double start = 0, end = -1; int pitch = 60, velocity = 100; };   // MIDI: end < 0 は押している途中
        std::vector<Note> notes;
    };

    const std::map<std::string, LiveRecording>& getLiveRecordings() const noexcept   { return liveRecordings; }

    /** 録音中の入力の大きさを集める（画面のタイマーから呼ぶ）。 */
    void pollRecording();

    /** Tracktion の Edit（Tracktion に「操作中の Edit」として知らせる用）。 */
    te::Edit& getEdit() noexcept   { return *edit; }

    /** 手動のレイテンシ補正（サンプル）。ドライバが報告するレイテンシの補正に加えてずらす。 */
    void setManualLatencySamples (int samples);
    /** 録音のタイミングの確認（--record-test）で使う仮想 MIDI 入力の名前。 */
    static constexpr const char* loopbackMidiName = "ShareDAW Loopback MIDI";
    /** MIDI の録音位置の補正（ミリ秒。正の値で録音を前＝早くずらす。オーディオの補正と同じ向き）。 */
    void setMidiRecordOffsetMs (double msEarlier);

    struct RecordedTake
    {
        std::string trackId;
        juce::File file;
        double startSeconds = 0, offsetSeconds = 0, lengthSeconds = 0;
        double punchInSeconds = 0;   // 録音を始めた位置（これより前はカウントイン）
        int channel = 0;             // ステレオのトラック: 0 = 左の入力、1 = 右の入力（取り込むときに 1 つのステレオのファイルにする）
        bool stereo = false;
    };

    /** 録音が終わったとき（メッセージスレッド）。受け取った側で audio/ に取り込み、元のファイルを消す。 */
    std::function<void (std::vector<RecordedTake>)> onRecordingFinished;

    //==============================================================================
    // MIDI キーボード（MIDI 入力）
    struct MidiInputStatus
    {
        juce::String name;
        bool enabled = false;
        float activity = 0.0f;   // 0〜1（弾いた強さ。少しずつ下がる）
    };

    std::vector<MidiInputStatus> getMidiInputs() const;

    /**
        MIDI トラックの入力（受ける MIDI 機器の名前。空ならすべての MIDI 入力、"-" ならなし）。この PC だけの設定。
        MIDI の録音先・試し弾きの先（録音待機、なければ選択中の MIDI トラック）がこの入力を受ける。
    */
    juce::String getTrackMidiInput (const std::string& trackId) const;
    void setTrackMidiInput (const std::string& trackId, const juce::String& device);
    void setMidiInputEnabled (const juce::String& name, bool enabled);

    /** 入力の強さを更新する（UI のタイマーから 30Hz 程度で呼ぶ）。 */
    void pollMidiActivity();

    /** MIDI キーボードで鳴らす・録音するトラック（選択中の MIDI トラック。空なら鳴らさない）。 */
    void setMidiTarget (const std::string& trackId);

    struct RecordedMidi
    {
        std::string trackId;
        std::vector<collab::Note> notes;   // tick はプロジェクトの先頭から
        collab::Tick punchInTick = 0;
    };

    /** MIDI の録音が終わったとき（メッセージスレッド）。 */
    std::function<void (std::vector<RecordedMidi>)> onMidiRecorded;

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
        juce::uint32 clipsRebuiltAt = 0;   // クリップを作り直した時刻（直後の試し弾きは少し待つ）
        juce::String problem;
        int missingAudio = 0;

        bool renderMode = false;
        te::Plugin::Ptr externalInstrument;
        std::string instrumentKey, instrumentStateRef;

        struct Effect { std::string id, stateRef; te::Plugin::Ptr plugin; };
        std::vector<Effect> effects;
        std::string effectsKey;

        // バス（出力先・センド）
        te::Plugin::Ptr auxReturn;             // バストラック: センドを受ける
        std::vector<te::Plugin::Ptr> sends;    // センド（AuxSend）
        std::string sendsKey;
    };

    te::Engine& engine;
    ProjectDocument& document;
    const InstrumentLibrary& library;
    std::shared_ptr<HostTempo> hostTempo = std::make_shared<HostTempo>();   // 外部プラグインに知らせる曲のテンポ（Follow Host）
    std::unique_ptr<te::Edit> edit;

    std::map<std::string, Binding> bindings;
    std::string tempoKey;

    te::AudioTrack::Ptr chordTrack;

    juce::Result renderOneTrack (const std::string& trackId, const juce::File& output, double endSeconds, bool asStem, int bitDepth);
    juce::BigInteger tracksMatching (const std::function<bool (te::Track*)>&) const;

    /**
        1 本のトラックだけを書き出すあいだの設定: 出力を直接にし（バスやミックス用のトラックを通さない）、センドを止め、ミュートを外す。
        dry（バウンス）なら音量・パンも 0 にし、strip（EQ・Comp）を外す。抜けるときに必ず元に戻す。
    */
    class ScopedIsolatedTrack
    {
    public:
        ScopedIsolatedTrack (te::AudioTrack&, std::vector<te::Plugin::Ptr>& sends, te::Plugin* stripToBypass, bool dry);
        ~ScopedIsolatedTrack();

    private:
        te::AudioTrack& track;
        std::vector<te::Plugin::Ptr>& sends;
        te::Plugin* strip = nullptr;
        te::VolumeAndPanPlugin* volume = nullptr;
        te::AudioTrack* oldDest = nullptr;
        float oldDb = 0.0f, oldPan = 0.0f;
        bool oldMute = false, stripWasEnabled = false;
        std::vector<bool> sendsWereEnabled;

        JUCE_DECLARE_NON_COPYABLE (ScopedIsolatedTrack)
    };
    bool renderTracksToWav (const juce::BigInteger& tracksToDo, const juce::File& output, double endSeconds, int bitDepth, double sampleRate);
    std::unique_ptr<Meter> chordMeter, metronomeMeter, masterMeter;
    float masterVolumeDb = 0.0f;
    SfizzPlugin* chordSynth = nullptr;
    juce::String chordSfzText;
    std::string chordKey;

    // 曲の音がまとまるミックスバス（マスターのリミッターとラウドネス計測。メトロノームは通さない）
    te::AudioTrack::Ptr mixTrack;
    class MasterLimiterPlugin* masterLimiter = nullptr;
    Binding masterEffects;   // マスターのエフェクト（ミックスバスのリミッターの前。track はミックスバス）
    collab::LoudnessStats loudnessStats;
    std::vector<double> loudnessScratch;
    bool loudnessWasPlaying = false;
    MasterStatus lastMasterStatus;
    juce::uint32 lastMasterPoll = 0;
    void routeToMix (te::AudioTrack&);
    void startLoudness();

    te::AudioTrack::Ptr metronomeTrack;
    std::string metronomeKey;
    bool metronomeEnabled = false;
    float metronomeVolumeDb = -6.0f;

    std::map<std::string, TrackInput> trackInputs;

    /** オーディオ機器の入力をそのまま測る（録音やトラックの割り当てに関係なく入力の信号を見る）。 */
    struct InputMeter  : public juce::AudioIODeviceCallback
    {
        static constexpr int maxChannels = 64;
        std::array<std::atomic<float>, maxChannels> peaks {};
        std::array<std::atomic<float>, maxChannels> recordingPeaks {};   // 録音中の表示用（ミキサーのメーターとは別に読む）

        /**
            オーディオの録音（入ってきた音をそのままファイルに書く）。
            位置は「このブロックで鳴らしている位置 − （入力 + 出力 + エンジン内の遅れ）」。
            Tracktion の録音は位置がバッファ 1〜2 個分前にずれる（録音のタイミングの確認: --record-test と SHAREDAW_LOOPBACK）ので、自前で書く。
            エンジン内の遅れ（プラグインの遅れの補正）も引く（再生の位置は、補正の分だけ先を処理している）。
        */
        struct Take
        {
            std::string trackId;
            std::array<int, 2> channels { -1, -1 };   // InputMeter の番号（有効なチャンネルだけを数えた順）
            int numChannels = 1;
            juce::File file;
            std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
            juce::int64 written = 0;

            // ブロックごとの「再生位置 − それまでに書いたサンプル数」（= 最初のサンプルの位置）。
            // 再生の始まりは位置がずれることがあるので、最後に真ん中の値（中央値）を使う
            std::array<juce::int64, 2048> anchors {};
            int numAnchors = 0;
            juce::int64 lastPosition = std::numeric_limits<juce::int64>::min();
            int lastBlock = 0;
        };

        struct Recorder
        {
            std::vector<std::unique_ptr<Take>> takes;
            tracktion::graph::PlayHead* playHead = nullptr;
            juce::int64 latency = 0;
            double sampleRate = 48000.0;
            std::vector<float> silence = std::vector<float> (16384, 0.0f);
        };

        juce::SpinLock recorderLock;     // 音の処理のスレッドは recorder を使う間これを持つ
        Recorder* recorder = nullptr;

        void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
        void audioDeviceStopped() override {}
    };

    InputMeter inputMeter;
    std::unique_ptr<InputMeter::Recorder> recorder;
    juce::TimeSliceThread takeWriterThread { "ShareDAW take writer" };
    double midiOutputLatencySeconds = 0.0;
    bool inputsChangedWhileRecording = false;   // 録音中に届いた入力の変更（録音が終わってから反映する）   // MIDI の録音: 聞こえる音は出力の遅れの分だけ後なので、その分を前へ戻す
    void startOwnRecording (const juce::File& dir);
    void finishOwnRecording();
    void deliverRecordings();

    struct MidiIn
    {
        std::shared_ptr<te::MidiInputDevice> device;
        std::unique_ptr<te::LevelMeasurer::Client> client;
        float activity = 0.0f;

        ~MidiIn()   { if (device != nullptr && client != nullptr) device->levelMeasurer.removeClient (*client); }
    };

    std::vector<std::unique_ptr<MidiIn>> midiInputs;
    std::string midiTargetId;
    double lastSetTick = 0.0, lastSetSeconds = -1.0;   // 最後に置いた再生位置（秒との往復の誤差をなくす）
    std::map<std::string, juce::String> trackMidiInputs;
    std::string spectrumTrackId;
    std::vector<RecordedMidi> pendingMidi;
    void refreshMidiInputs();
    CountInPlugin* countIn = nullptr;
    std::vector<RecordedTake> pendingTakes;
    int manualLatencySamples = 0;
    double midiRecordOffsetMs = 0.0;
    double punchInSeconds = 0;
    std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true);

    std::map<std::string, LiveRecording> liveRecordings;
    juce::SharedResourcePointer<te::MidiInputDevice::MidiKeyChangeDispatcher> midiKeyDispatcher;
    void midiKeyStateChanged (te::AudioTrack*, const juce::Array<int>& notesOn, const juce::Array<int>& velocities, const juce::Array<int>& notesOff) override;
    int activeInputIndex (const juce::String& waveInputName) const;

    bool loopEnabled = false;
    collab::Tick loopStart = 0, loopEnd = 0;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    te::AudioTrack::Ptr createTrack();
    SfizzPlugin* addSynth (te::AudioTrack&);
    void syncTrack (const collab::Track&, Binding&, bool tempoChanged);
    void syncInstrument (const collab::Track&, Binding&);
    void syncEffects (const collab::Track&, Binding&);
    void syncEffects (const std::vector<collab::Effect>&, Binding&, te::Plugin* insertBefore);
    void syncStrip (const collab::Track&, Binding&);
    void syncRouting (const collab::Project&);
    void removeInstrument (Binding&);
    void removeEffects (Binding&);
    bool canPlayLive (const collab::Track&, juce::String& why) const;
    te::Plugin::Ptr createExternal (const collab::ExternalPlugin&, const std::string& stateRef);
    void syncMetronome (bool tempoChanged);
    void syncChordTrack (bool tempoChanged);
    void applyLoop();
    void applyInputs();

    /** MIDI キーボードの音を、選択中のトラックの音源だけに通す（LiveMidiGate。再生の処理は作り直さない）。 */
    void updateLiveMidiGates();
    juce::String liveMidiTracksKey;   // MIDI キーボードをつないでいる MIDI トラック
    bool isLiveMidiInput (te::InputDeviceInstance&);
    void configureInputs();
    void restoreAfterRecording();

    void recordingStopped (te::SyncPoint, bool discardRecordings) override;
    void recordingFinished (te::InputDeviceInstance&, te::EditItemID targetID,
                            const juce::ReferenceCountedArray<te::Clip>& recordedClips) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineBridge)
};
