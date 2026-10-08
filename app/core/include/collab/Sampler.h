#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Project.h"

namespace collab
{

/**
    内蔵サンプラー（builtin.sampler）の 1 つのパッド。曲に読み込んだオーディオ（audio/ の実体、ハッシュで指す）を、
    決まったノートで鳴らす。設定は Instrument::params.pads（JSON）に入り、曲と一緒に同期されるので、
    ほかの人も MIDI を打ち込めば同じ音が鳴る。
*/
struct SamplerPad
{
    int note = 36;                  // 鳴らすノート（MIDI ノート番号）
    std::string audioHash;          // 空ならパッドは空き
    std::string name;               // 表示名（読み込んだファイル名など）
    double gainDb = 0.0;
    double pan = 0.0;               // -1〜1
    double tuneSemitones = 0.0;     // -24〜24
    bool oneShot = true;            // true: 最後まで鳴らす（ドラム・効果音）。false: ノートを離したら止める
    int chokeGroup = 0;             // 0 = なし。同じ番号のパッドは後から鳴った方が前を止める（オープン / クローズのハイハットなど）
    double sourceBpm = 0.0;         // 元の素材のテンポ。0 より大きければ、曲のテンポに合わせて伸び縮みさせた音を鳴らす

    bool operator== (const SamplerPad&) const = default;
};

inline constexpr int kSamplerPads = 16;

/** params からパッドを 16 個読む（ないパッドは空き。ノートは 36 から順に）。 */
std::vector<SamplerPad> samplerPads (const nlohmann::json& params);

/** パッドを書き戻した params（ほかの値はそのまま）。 */
nlohmann::json withSamplerPads (const nlohmann::json& params, const std::vector<SamplerPad>&);

/**
    サンプラーの SFZ。samplePath はパッドの実体のファイルの絶対パスを返す（まだない・見つからないときは空。そのパッドは鳴らさない）。
    テンポに合わせるパッドは、伸び縮みさせたファイルを返してよい。
*/
std::string generateSamplerSfz (const nlohmann::json& params, const std::function<std::string (const SamplerPad&)>& samplePath);

/** 曲が使っているオーディオの実体（クリップ・バウンス・サンプラーのパッド）。同期でアップ・ダウンロードするもの。 */
std::vector<std::string> referencedAudio (const Project&);

bool isSampler (const Instrument&);

}
