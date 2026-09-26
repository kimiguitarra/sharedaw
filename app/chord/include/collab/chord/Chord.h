#pragma once

// コード表記のパーサーとボイシング（仕様書 §3.8）。
// 将来ほかのアプリ（コード進行メモアプリ）でも使えるよう、ShareDAW の他の部分には依存しない。

#include <optional>
#include <string>
#include <vector>

namespace collab::chord
{

/** 正規化されたコード。 */
struct Chord
{
    std::string root;                    // "C", "C#", "Db" ...（入力の綴りを保持。♯/♭ は #/b に正規化）
    std::string quality;                 // 下の Quality 名のいずれか
    std::vector<std::string> tensions;   // "b9","9","#9","11","#11","b13","13"（この順に並ぶ）
    std::optional<std::string> bass;     // 分数コードのベース音

    bool operator== (const Chord&) const = default;
};

/**
    コードタイプ（quality）の正規名:
      三和音: "maj" "m" "dim" "aug" "sus2" "sus4"
      四和音: "6" "m6" "7" "maj7" "m7" "mM7" "m7b5" "dim7" "7sus4"
      その他: "add9"
*/
const std::vector<std::string>& allQualities();
const std::vector<std::string>& allTensions();

struct ParseResult
{
    std::optional<Chord> chord;
    bool noChord = false;        // "X" / "N.C."
    std::string error;           // 失敗時の理由（日本語）

    bool ok() const noexcept     { return chord.has_value() || noChord; }
};

/** 表記を解釈する。例: "G7(9,13)", "Am7/G", "F#m7-5", "C△7", "Bb9", "X" */
ParseResult parse (const std::string& text);

/** 表示用の表記に整形する。例: CM7(9), Am7(b5), F/G */
std::string format (const Chord&);

/** 音名 → ピッチクラス（0..11）。不正なら -1。 */
int pitchClass (const std::string& noteName);

/** ルートからの音程（半音、テンションは 9 度 = 14 のように 12 以上）を返す。ベースは含めない。 */
std::vector<int> chordIntervals (const Chord&);

//==============================================================================
/** 1つのコードの発音（ベース1音 + 上声部3〜4音、MIDI ノート番号・昇順）。 */
struct Voicing
{
    int bass = 36;
    std::vector<int> upper;

    bool operator== (const Voicing&) const = default;
};

struct VoicingRules
{
    int bassLow = 36, bassHigh = 47;       // C2〜B2
    int upperLow = 52, upperHigh = 76;     // E3〜E5
    int maxUpperVoices = 4;
    int firstChordCentre = 64;
    int maxSpan = 14;                      // 上声部の最低音〜最高音の幅
};

/**
    ルールベースでボイシングを決める。
    previous があれば、上声部の移動量の合計が最小になるものを選ぶ（ボイスリーディング）。
*/
Voicing voice (const Chord&, const Voicing* previous = nullptr, const VoicingRules& = {});

/** コード進行をまとめてボイシングする（ノーコードは std::nullopt、その前後はつながりを切らない）。 */
std::vector<std::optional<Voicing>> voiceProgression (const std::vector<std::optional<Chord>>&, const VoicingRules& = {});

} // namespace collab::chord
