#pragma once

// 内蔵エフェクト（インサートに挿す）の信号処理。JUCE に依存しない（テストから使えるように）。
//   busComp     バスコンプ（SSL 4000G のバスコンプの動きを参考にした VCA コンプ）
//   saturator   サチュレーター（テープ・真空管のように偶数次の倍音が出る、温かみのある歪み）
//   roomReverb  ショートルームのリバーブ
//   hallReverb  ホールのリバーブ
//   plateReverb プレートのリバーブ
// 値はプロジェクト JSON の effect.params（キーは ParamSpec::key）に持つ。

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace collab::fx
{

enum class Type { busComp, saturator, roomReverb, hallReverb, plateReverb };

/** JSON の builtin の値（"busComp" など）。 */
std::string idOf (Type);
std::optional<Type> typeFromId (const std::string&);
const std::vector<Type>& allTypes();

/** 画面に出す名前。 */
std::string displayName (Type);   // utf8-std

struct ParamSpec
{
    std::string key;
    std::string label;                  // 画面の名前（日本語可）
    double min = 0, max = 1, def = 0;
    std::string unit;                   // "dB" "ms" "s" "Hz" "%"
    std::vector<std::string> choices;   // 空でなければ選択式（値は番号 0, 1, …）
    double skewMid = 0;                 // つまみの真ん中に来る値（0 なら直線）
};

const std::vector<ParamSpec>& paramSpecs (Type);

/** params から値を読む（なければ既定値、範囲外は収める）。 */
double paramValue (const nlohmann::json& params, const ParamSpec&);
double paramValue (Type, const nlohmann::json& params, const std::string& key);

/** 既定値だけの params（トラックに挿したとき）。isBus ならリバーブの MIX を 100%（センドで使う想定）にする。 */
nlohmann::json defaultParams (Type, bool isBus);

/** ブロックを処理する（setParams と process は同じスレッドから）。 */
class Processor
{
public:
    virtual ~Processor() = default;

    virtual void prepare (double sampleRate) = 0;
    virtual void reset() = 0;
    virtual void setParams (const nlohmann::json& params) = 0;

    /** channels[ch][i] をその場で処理する（1 または 2 チャンネル）。 */
    virtual void process (float* const* channels, int numChannels, int numSamples) = 0;

    /** 入力が止まっても音が続く長さ（秒）。バウンス・書き出しの余韻に使う。 */
    virtual double tailSeconds() const          { return 0.0; }

    /** コンプのゲインリダクション（dB、0 以上）。 */
    float getGainReductionDb() const noexcept   { return gainReductionDb.load (std::memory_order_relaxed); }

protected:
    std::atomic<float> gainReductionDb { 0.0f };
};

std::unique_ptr<Processor> createProcessor (Type);

}
