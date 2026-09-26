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

struct DrumPiece
{
    std::string key;          // "kick", "snare", ...（params.pieces のキー）
    std::string displayName;  // 表示名
    int note = 36;            // 発音する MIDI ノート（GM ドラムマップ）
    std::string sfzExtra;     // チョーク等、SFZ の追加オプコード
};

struct BuiltinInstrumentManifest
{
    std::string id;           // "builtin.drums"
    std::string version;      // "0.1.0"
    std::string displayName;
    std::string type;         // "drums" | "melodic"
    std::string mainSfz;      // melodic のときのメイン SFZ（相対パス）
    std::vector<DrumPiece> pieces;                                    // drums のみ
    std::map<std::string, std::map<std::string, std::string>> kits;   // kit -> piece -> sample
    std::vector<std::string> samples;                                 // 差し替え可能なサンプル ID
    std::vector<std::string> credits;                                 // CC-BY 等のクレジット表記
    nlohmann::json defaultParams = nlohmann::json::object();

    InstrumentRef ref() const     { return { id, version }; }
    const DrumPiece* findPiece (const std::string& key) const;

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
    };

    std::string kit;
    std::map<std::string, Piece> pieces;
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
}

} // namespace collab
