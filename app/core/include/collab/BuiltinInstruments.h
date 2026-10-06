#pragma once

// 内蔵音源（§3.3）のマニフェストと、パラメータから sfizz 用 SFZ テキストを生成する処理。
// 音源ファイルは /assets/instruments/<id>/<version>/ に置き、バージョン単位で固定配布する。

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Project.h"

namespace collab
{

/** パーツを鳴らす別のノート（Superior Drummer 3 のマップなど）。 */
struct DrumNoteAlias
{
    int note = 0;
    std::string displayName;  // 例: "ハイハット（クローズ・エッジ）"
    std::string sfzExtra;     // このノートだけの音の違い（開き具合・チップ / シャンクなど。SFZ のオプコード）
};

/** パーツの音の選び方（スネアの胴の深さ・シェルなど）。params.pieces.<key>.<option key> に選んだ choice の key を持つ。 */
struct DrumPieceOption
{
    struct Choice
    {
        std::string key;
        std::string displayName;
        std::string sfz;      // この選び方の SFZ のオプコード（空なら元の音のまま）
    };

    std::string key;          // "depth" など
    std::string displayName;  // "胴の深さ"
    std::string defaultChoice;
    std::vector<Choice> choices;
};

struct DrumPiece
{
    std::string key;          // "kick", "snare", ...（params.pieces のキー）
    std::string displayName;  // 表示名
    int note = 36;            // 発音する MIDI ノート（GM ドラムマップ）
    std::string sfzExtra;     // チョーク等、SFZ の追加オプコード
    std::vector<DrumNoteAlias> aliases;   // 同じ音で鳴る別のノート
    std::vector<DrumPieceOption> options; // 音の選び方
    std::string optionsFrom;              // ほかのパーツの選び方に従う（リムショットはスネアと同じ胴・シェル）
};

/** 旋律楽器の音色（プリセット）。params.preset で選ぶ。 */
struct InstrumentPreset
{
    std::string key;          // params.preset の値
    std::string displayName;
    std::string sfz;          // 音色の SFZ（マニフェストのフォルダからの相対パス）
    double volumeDb = 0.0;    // 音色ごとの音量の補正（ほかの内蔵音源と同じくらいの大きさにする）
};

struct BuiltinInstrumentManifest
{
    std::string id;           // "builtin.drums"
    std::string version;      // "0.1.0"
    std::string samplesFrom;  // サンプルを置いている版（空なら version。前の版のサンプルをそのまま使う新しい版で指定する）
    std::string displayName;
    std::string type;         // "drums" | "melodic"
    std::string mainSfz;      // melodic のときのメイン SFZ（相対パス。presets があれば不要）
    std::vector<InstrumentPreset> presets;                            // melodic の音色
    std::vector<DrumPiece> pieces;                                    // drums のみ
    std::map<std::string, std::map<std::string, std::string>> kits;   // kit -> piece -> sample
    std::vector<std::string> kitOrder;                                // 画面に並べる順（kitOrder が無ければ名前順）
    std::map<std::string, std::string> kitAliases;                    // 前の版のキット名 -> この版のキット名（名前を変えたとき）
    std::vector<std::string> samples;                                 // 差し替え可能なサンプル ID
    std::vector<std::string> credits;                                 // CC-BY 等のクレジット表記
    nlohmann::json defaultParams = nlohmann::json::object();

    InstrumentRef ref() const     { return { id, version }; }
    const std::string& sampleVersion() const   { return samplesFrom.empty() ? version : samplesFrom; }
    const DrumPiece* findPiece (const std::string& key) const;
    /** このノートで鳴るパーツ（別名のノートを含む）。name にはそのノートの表示名を入れる。 */
    const DrumPiece* findPieceForNote (int note, std::string* name = nullptr) const;

    /** manifest.json の内容から読み込む。不正なら std::runtime_error。 */
    static BuiltinInstrumentManifest fromJson (const nlohmann::json&);
};

/** params をマニフェストの既定値で補完したもの。 */
struct ResolvedInstrumentParams
{
    double volumeDb = 0.0;
    double pan = 0.0;             // -1〜1
    double toneDb = 0.0;          // 高域シェルフ（-12〜+12 dB）

    struct Piece
    {
        std::string sample;
        double volumeDb = 0.0;
        double pan = 0.0;
        double tuneSemitones = 0.0;
        std::map<std::string, std::string> options;   // 選び方（option key -> choice key）
    };

    std::string kit;
    std::map<std::string, Piece> pieces;

    std::string preset;           // melodic で presets があるとき
};

ResolvedInstrumentParams resolveInstrumentParams (const BuiltinInstrumentManifest&, const nlohmann::json& params);

/**
    sfizz に渡す SFZ テキストを生成する。
    返したテキストは、マニフェストのあるフォルダに置いた仮想ファイルとして loadSfzString() に渡す
    （#include はそのフォルダからの相対パスで解決される）。
    全体の音量・パンは含めない（プラグイン側で適用し、SFZ の再読み込みを避ける）。
*/
std::string generateSfz (const BuiltinInstrumentManifest&, const nlohmann::json& params);

/** 既定の内蔵音源 ID。 */
namespace builtin
{
    inline constexpr const char* drums = "builtin.drums";
    inline constexpr const char* bass  = "builtin.bass";
    inline constexpr const char* piano = "builtin.piano";
    inline constexpr const char* epiano = "builtin.epiano";
}

} // namespace collab
