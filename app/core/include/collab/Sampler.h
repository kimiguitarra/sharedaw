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
    int note = 36;                  // 鳴らすノート（白鍵。パッドの番号で決まる）
    std::string audioHash;          // 空ならパッドは空き
    std::string name;               // 表示名（ピアノロールの左にも出る）
    double gainDb = 0.0;
    double pan = 0.0;               // -1〜1
    double tuneSemitones = 0.0;     // -24〜24。長さは変えずに高さだけ変える（前もって作ったファイルを鳴らす）
    bool oneShot = true;            // true: 最後まで鳴らす（ドラム・効果音）。false: ノートを離したら止める
    int chokeGroup = 0;             // 0 = なし。同じ番号のパッドは後から鳴った方が前を止める（オープン / クローズのハイハットなど）
    double sourceBpm = 0.0;         // 元の素材のテンポ。0 より大きければ、曲のテンポに合わせて伸び縮みさせた音を鳴らす
    SampleCount startSamples = 0;   // 元ファイルの使う範囲（頭を切る）
    SampleCount endSamples = 0;     // 使う範囲の終わり（0 = ファイルの終わりまで）
    double eqLowDb = 0.0;           // EQ: ローシェルフ 100Hz
    double eqMidDb = 0.0;           // EQ: ピーキング（eqMidHz）
    double eqMidHz = 1000.0;
    double eqHighDb = 0.0;          // EQ: ハイシェルフ 8kHz
    double driveDb = 0.0;           // サチュレーション（0 = なし、〜24dB）。音量は変えない

    bool operator== (const SamplerPad&) const = default;
};

inline constexpr int kSamplerPads = 16;

/** パッドのノート: C2（36）から白鍵を 16 個（C2〜D4）。 */
int samplerPadNote (int index);

/** params からパッドを 16 個読む（ないパッドは空き。ノートは samplerPadNote）。 */
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

/** そのまま鳴らせず、前もって加工したファイルが要るか（切る・高さ・テンポ・EQ・サチュレーション）。 */
bool padNeedsRender (const SamplerPad&, double speed);

/** 加工したファイルを見分ける文字列（設定が同じなら同じ。ファイル名に使う）。 */
std::string padRenderKey (const SamplerPad&, double speed);

/**
    パッドの EQ とサチュレーションを channels にかける（切る・高さ・テンポは済んだもの）。
    サチュレーションは前後で音の大きさ（RMS）をそろえる。
*/
void processPadAudio (std::vector<std::vector<float>>& channels, double sampleRate, const SamplerPad&);

}
