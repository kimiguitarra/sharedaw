#pragma once

// キーとディグリー（度数）。コード → ディグリー表記（Ⅵm7 など）と、ディグリー入力（6m7 など）→ コード表記の変換。

#include <optional>
#include <string>
#include <vector>

#include "Chord.h"

namespace collab::chord
{

/** キー（主音のピッチクラス 0..11 とメジャー / マイナー）。 */
struct Key
{
    int tonic = 0;
    bool minor = false;

    bool operator== (const Key&) const = default;
};

/** キーの名前。例: "C", "F#", "Bb", "Am", "C#m"。 */
std::string keyName (const Key&);

/** キーの調号に合わせた音名（フラット系のキーは Bb、シャープ系のキーは F# のように）。 */
std::string spellPitch (int pitchClass, const Key&);

/**
    コードのディグリー表記（キーの音階が基準。音階にない音は ♭ / ♯ を付ける）。
    例: C キーで Am7 → "Ⅵm7"、Bb → "♭Ⅶ"、C/E → "Ⅰ/Ⅲ"。A マイナーで C → "Ⅲ"、E7 → "Ⅴ7"。
*/
std::string degreeName (const Chord&, const Key&);

/**
    ディグリーの入力をキーに合わせたコード表記に変える。ディグリーでなければ std::nullopt。
    - 数字 1〜7（全角も可）またはローマ数字（I〜VII、Ⅰ〜Ⅶ）。前に b / ♭ / # / ♯ を付けられる
    - 後ろに何も付けなければキーの音階の四和音（C キーで 1 → CM7、2 → Dm7、5 → G7、7 → Bm7(b5)）。小文字のローマ数字は m7
    - 後ろにコードタイプを付けるとそのまま（C キーで 6m → Am、57 → G7、4M7 → FM7、1maj → C。b7 → Bb）
    - 分数コードのベースもディグリーで書ける（1/3 → C/E）
*/
std::optional<std::string> degreeToChordText (const std::string& input, const Key&);

/**
    コード進行からキーを推定する（コードの構成音がどれだけ音階に収まるか＋最初・最後のコードが主和音か）。
    コードがなければ std::nullopt。
*/
std::optional<Key> estimateKey (const std::vector<Chord>&);

} // namespace collab::chord
