#pragma once

// プロジェクトの正（source of truth）となるデータモデル。仕様書 §7。
// JUCE / Tracktion に依存しない純粋な C++ で書く（サーバー側・テストからも使えるように）。

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace collab
{

using Tick = std::int64_t;
using SampleCount = std::int64_t;

inline constexpr int kSchemaVersion = 1;
inline constexpr int kPpq = 960;
inline constexpr int kSampleRate = 48000;

//==============================================================================
struct TempoEvent
{
    std::string id;
    Tick tick = 0;
    double bpm = 120.0;

    bool operator== (const TempoEvent&) const = default;
};

struct MeterEvent
{
    std::string id;
    int bar = 1;          // 1 始まり
    int numerator = 4;
    int denominator = 4;

    bool operator== (const MeterEvent&) const = default;
};

struct TempoTrack
{
    std::string id;
    std::vector<TempoEvent> events;

    bool operator== (const TempoTrack&) const = default;
};

struct MeterTrack
{
    std::string id;
    std::vector<MeterEvent> events;

    bool operator== (const MeterTrack&) const = default;
};

//==============================================================================
struct InstrumentRef
{
    std::string id;       // 例: "builtin.piano"
    std::string version;  // 例: "1.0.0"

    bool operator== (const InstrumentRef&) const = default;
};

struct ChordSymbol
{
    std::string root;                    // "C", "F#", "Bb" ...
    std::string quality;                 // "maj", "m", "maj7", "7", ...
    std::vector<std::string> tensions;   // "9", "b9", "#11" ...
    std::optional<std::string> bass;     // 分数コードのベース

    bool operator== (const ChordSymbol&) const = default;
};

struct ChordEvent
{
    std::string id;
    Tick tick = 0;
    bool noChord = false;
    std::optional<ChordSymbol> chord;
    std::string text;

    bool operator== (const ChordEvent&) const = default;
};

struct ChordPlayback
{
    bool enabled = true;
    double volumeDb = -6.0;
    InstrumentRef instrument;

    bool operator== (const ChordPlayback&) const = default;
};

struct ChordTrack
{
    std::string id;
    ChordPlayback playback;
    std::vector<ChordEvent> events;

    bool operator== (const ChordTrack&) const = default;
};

/** マーカー（Cubase のマーカートラック）。番号は左から順に振る（保存しない）。 */
struct Marker
{
    std::string id;
    Tick tick = 0;
    std::string name;       // 空でもよい

    bool operator== (const Marker&) const = default;
};

struct MarkerTrack
{
    std::string id;
    std::vector<Marker> events;

    bool operator== (const MarkerTrack&) const = default;
};

/** マーカートラックの ID（プロジェクト ID から決まる。古いプロジェクトでも全員で同じ ID になるように）。 */
std::string markerTrackIdFor (const std::string& projectId);

/** キー（調）の変更。小節の頭に置く。コードのディグリー表示・ディグリー入力の基準になる。 */
struct KeyEvent
{
    std::string id;
    int bar = 1;            // 1 始まり
    int tonic = 0;          // 主音のピッチクラス（0 = C … 11 = B）
    bool minor = false;

    bool operator== (const KeyEvent&) const = default;
};

struct KeyTrack
{
    std::string id;
    std::vector<KeyEvent> events;   // 空ならキー未設定

    bool operator== (const KeyTrack&) const = default;
};

/** キートラックの ID（マーカートラックと同じくプロジェクト ID から決まる）。 */
std::string keyTrackIdFor (const std::string& projectId);

/**
    マスターのリミッター（ヴィンテージ系リミッターの操作感）。
    THRESHOLD を下げるほど入力が持ち上がり、CEILING を超えないように抑える。CHARACTER は速さ（0 = ゆっくり・なめらか、10 = 速い）。
*/
enum class LimiterMode { analog, tube, modern };

struct MasterLimiter
{
    bool enabled = false;
    double thresholdDb = 0.0;       // 0〜-20 dB
    double ceilingDb = -1.0;        // 出力の上限（-3〜0 dB）
    double character = 5.0;         // 0〜10
    LimiterMode mode = LimiterMode::analog;

    bool operator== (const MasterLimiter&) const = default;
};

/** マスター（全員で共通の設定。スコープとしてロック・差分の単位になる）。 */
struct MasterBus
{
    std::string id;
    MasterLimiter limiter;

    bool isDefault() const noexcept     { return limiter == MasterLimiter(); }
    bool operator== (const MasterBus&) const = default;
};

/** マスターの ID（マーカートラックと同じくプロジェクト ID から決まる）。 */
std::string masterBusIdFor (const std::string& projectId);

std::string limiterModeName (LimiterMode);
LimiterMode limiterModeFromName (const std::string&);

//==============================================================================
struct ExternalPlugin
{
    std::string format;   // "VST3" | "AU"
    std::string name;
    std::string vendor;
    std::string uid;
    std::string os;       // "windows" | "mac"

    bool operator== (const ExternalPlugin&) const = default;
};

struct Instrument
{
    enum class Kind { builtin, external };

    Kind kind = Kind::builtin;

    // kind == builtin
    std::string id;
    std::string version;
    nlohmann::json params = nlohmann::json::object();

    // kind == external
    ExternalPlugin plugin;
    std::string stateRef;

    bool operator== (const Instrument&) const = default;
};

struct Effect
{
    std::string id;
    ExternalPlugin plugin;
    std::string stateRef;
    bool bypass = false;

    bool operator== (const Effect&) const = default;
};

/**
    各トラックに標準で付いている EQ とコンプ（チャンネルストリップ）。
    挿す位置は音源・エフェクトの後、音量・パンの前。バウンスには含めない（受け取った側でも同じ設定がかかるため）。
*/
struct ChannelEq
{
    bool enabled = false;
    double lowCutHz = 0.0;          // ローカット（0 = オフ）
    double highCutHz = 0.0;         // ハイカット（0 = オフ）
    double lowGainDb = 0.0;         // ローシェルフ
    double lowFreqHz = 100.0;
    double lowMidGainDb = 0.0;      // ピーキング（低め）
    double lowMidFreqHz = 300.0;
    double lowMidQ = 1.0;
    double midGainDb = 0.0;         // ピーキング
    double midFreqHz = 1000.0;
    double midQ = 1.0;
    double highGainDb = 0.0;        // ハイシェルフ
    double highFreqHz = 8000.0;

    bool operator== (const ChannelEq&) const = default;
};

enum class CompType { fet, opto };

struct ChannelComp
{
    bool enabled = false;
    CompType type = CompType::fet;  // FET（速い・色付け）／オプティカル（ゆっくり・自然）
    double thresholdDb = -18.0;
    double ratio = 4.0;
    double attackMs = 1.0;          // FET のみ（オプティカルは音に応じて自動）
    double releaseMs = 150.0;       // FET のみ
    double makeupDb = 0.0;

    bool operator== (const ChannelComp&) const = default;
};

struct ChannelStrip
{
    ChannelEq eq;
    ChannelComp comp;

    bool isDefault() const          { return *this == ChannelStrip {}; }
    bool operator== (const ChannelStrip&) const = default;
};

std::string compTypeName (CompType);

struct Render
{
    std::string audioHash;
    std::string renderedAt;         // UTC ISO 8601
    std::string sourceFingerprint;
    double tailSeconds = 2.0;

    bool operator== (const Render&) const = default;
};

//==============================================================================
struct Note
{
    std::string id;
    Tick tick = 0;          // クリップ先頭からの相対 tick
    Tick lengthTick = 240;
    int pitch = 60;
    int velocity = 100;

    Tick endTick() const noexcept     { return tick + lengthTick; }

    bool operator== (const Note&) const = default;
};

struct MidiClip
{
    std::string id;
    Tick startTick = 0;
    Tick lengthTick = kPpq * 4;
    std::vector<Note> notes;

    Tick endTick() const noexcept     { return startTick + lengthTick; }

    bool operator== (const MidiClip&) const = default;
};

struct AudioClip
{
    std::string id;
    Tick startTick = 0;
    std::string audioHash;
    std::string displayName;
    SampleCount sourceOffsetSamples = 0;
    SampleCount lengthSamples = 0;
    double gainDb = 0.0;
    SampleCount fadeInSamples = 0;
    SampleCount fadeOutSamples = 0;

    bool operator== (const AudioClip&) const = default;
};

enum class TrackType { midi, audio, bus };   // bus: グループ・FX 用のバス（クリップを持たず、他のトラックの出力・センドを受ける）

/** センド（トラックの音の一部をバスへ送る）。 */
struct Send
{
    std::string busId;
    double levelDb = 0.0;
    bool preFader = false;   // true: 音量フェーダーの前から送る

    bool operator== (const Send&) const = default;
};

struct Track
{
    std::string id;
    TrackType type = TrackType::midi;
    std::string name;
    std::string color = "#90A4AE";
    double volumeDb = 0.0;
    double pan = 0.0;
    bool mute = false;
    bool solo = false;

    std::optional<Instrument> instrument;   // midi トラックのみ
    std::vector<Effect> effects;
    ChannelStrip strip;
    std::string output;                     // 出力先のバストラックの ID（空ならマスター）
    std::vector<Send> sends;
    std::optional<Render> render;

    std::vector<MidiClip> midiClips;        // type == midi
    std::vector<AudioClip> audioClips;      // type == audio

    const MidiClip* findMidiClip (const std::string& clipId) const;
    MidiClip* findMidiClip (const std::string& clipId);

    bool operator== (const Track&) const = default;
};

//==============================================================================
struct Project
{
    int schemaVersion = kSchemaVersion;
    std::string projectId;
    std::string name;
    int sampleRate = kSampleRate;
    int ppq = kPpq;

    TempoTrack tempoTrack;
    MeterTrack meterTrack;
    ChordTrack chordTrack;
    MarkerTrack markerTrack;
    KeyTrack keyTrack;
    MasterBus master;
    std::vector<Track> tracks;

    const Track* findTrack (const std::string& trackId) const;
    Track* findTrack (const std::string& trackId);
    int indexOfTrack (const std::string& trackId) const;

    /** 全クリップ・コードイベントの末尾 tick（プロジェクトの「末尾」）。 */
    Tick contentEndTick() const;

    /** 配列を決定的な順序に並べ替える（§7.1）。保存前に必ず呼ぶ。 */
    void sortCanonical();

    bool operator== (const Project&) const = default;

    /** 空の新規プロジェクト（120BPM, 4/4, コードトラック空）。 */
    static Project createEmpty (const std::string& name);
};

std::string trackTypeName (TrackType);

} // namespace collab
