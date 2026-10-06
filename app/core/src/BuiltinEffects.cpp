#include "collab/BuiltinEffects.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "collab/ChannelStripDsp.h"

namespace collab::fx
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    double dbToGain (double db)            { return std::pow (10.0, db / 20.0); }
    double gainToDb (double g)             { return 20.0 * std::log10 (std::max (g, 1.0e-9)); }

    /** 時定数（秒）から 1 次の平滑化の係数。 */
    double coefficient (double seconds, double sampleRate)
    {
        return seconds <= 0.0 ? 0.0 : std::exp (-1.0 / (seconds * sampleRate));
    }

    const std::vector<std::string> onOff { "OFF", "ON" };

    //==============================================================================
    /** 伸び縮みする遅延線（小数の遅延は直線補間）。 */
    class DelayLine
    {
    public:
        void setMaxSamples (int n)
        {
            buffer.assign ((size_t) std::max (4, n + 4), 0.0f);
            write = 0;
        }

        void clear()                                { std::fill (buffer.begin(), buffer.end(), 0.0f); }
        void push (float v) noexcept                { buffer[(size_t) write] = v; write = (write + 1) % (int) buffer.size(); }

        /** delay サンプル前の値（push した直後なら delay = 1 が直前の値）。 */
        float read (double delay) const noexcept
        {
            const int size = (int) buffer.size();
            delay = std::clamp (delay, 1.0, (double) size - 2.0);
            const int i = (int) delay;
            const float frac = (float) (delay - i);
            const float a = buffer[(size_t) ((write - i + size) % size)];
            const float b = buffer[(size_t) ((write - i - 1 + size) % size)];
            return a + (b - a) * frac;
        }

    private:
        std::vector<float> buffer;
        int write = 0;
    };

    /** シュレーダーの全域通過フィルタ（音を散らす）。 */
    class Allpass
    {
    public:
        void setup (int samples, float g)           { line.setMaxSamples (samples + 2); length = samples; gain = g; }
        void clear()                                { line.clear(); }

        float process (float in) noexcept
        {
            const float delayed = line.read ((double) length);
            const float v = in + gain * delayed;
            line.push (v);
            return delayed - gain * v;
        }

    private:
        DelayLine line;
        int length = 1;
        float gain = 0.5f;
    };
}

//==============================================================================
std::string idOf (Type t)
{
    switch (t)
    {
        case Type::busComp:     return "busComp";
        case Type::saturator:   return "saturator";
        case Type::roomReverb:  return "roomReverb";
        case Type::hallReverb:  return "hallReverb";
        case Type::plateReverb: return "plateReverb";
        case Type::noiseGate:   return "noiseGate";
    }

    return {};
}

std::optional<Type> typeFromId (const std::string& id)
{
    for (auto t : allTypes())
        if (idOf (t) == id)
            return t;

    return std::nullopt;
}

const std::vector<Type>& allTypes()
{
    static const std::vector<Type> types { Type::busComp, Type::saturator, Type::noiseGate, Type::roomReverb, Type::hallReverb, Type::plateReverb };
    return types;
}

std::string displayName (Type t)
{
    switch (t)
    {
        case Type::busComp:     return "Bus Comp（4000G 風）";
        case Type::saturator:   return "Warm Saturator";
        case Type::roomReverb:  return "Room Reverb";
        case Type::hallReverb:  return "Hall Reverb";
        case Type::plateReverb: return "Plate Reverb";
        case Type::noiseGate:   return "Noise Gate";
    }

    return {};
}

const std::vector<ParamSpec>& paramSpecs (Type t)
{
    // SSL 4000G のバスコンプと同じ段階のつまみ（アタック・リリース・レシオ）。スレッショルドは dBFS
    static const std::vector<ParamSpec> busComp {
        { "threshold", "THRESHOLD", -40.0, 0.0, -12.0, "dB", {}, 0 },
        { "ratio", "RATIO", 0, 2, 1, "", { "2", "4", "10" }, 0 },
        { "attack", "ATTACK", 0, 5, 4, "ms", { "0.1", "0.3", "1", "3", "10", "30" }, 0 },
        { "release", "RELEASE", 0, 4, 4, "s", { "0.1", "0.3", "0.6", "1.2", "AUTO" }, 0 },
        { "makeup", "MAKE UP", 0.0, 20.0, 0.0, "dB", {}, 0 },
        { "sidechainHpf", "S/C HPF", 0, 3, 0, "Hz", { "OFF", "60", "90", "150" }, 0 },
        { "mix", "MIX", 0.0, 100.0, 100.0, "%", {}, 0 },
    };

    static const std::vector<ParamSpec> saturator {
        { "drive", "DRIVE", 0.0, 24.0, 6.0, "dB", {}, 0 },
        { "warmth", "WARMTH", 0.0, 100.0, 50.0, "%", {}, 0 },
        { "output", "OUTPUT", -18.0, 6.0, 0.0, "dB", {}, 0 },
        { "mix", "MIX", 0.0, 100.0, 100.0, "%", {}, 0 },
    };

    // ゲート: スレッショルドを超えたら開き、下回ってホールドの後に閉じる。閉じたときは RANGE だけ下げる
    static const std::vector<ParamSpec> gate {
        { "threshold", "THRESHOLD", -80.0, 0.0, -50.0, "dB", {}, 0 },
        { "range", "RANGE", 0.0, 80.0, 80.0, "dB", {}, 0 },
        { "attack", "ATTACK", 0.1, 50.0, 1.0, "ms", {}, 5.0 },
        { "hold", "HOLD", 0.0, 500.0, 50.0, "ms", {}, 80.0 },
        { "release", "RELEASE", 5.0, 2000.0, 150.0, "ms", {}, 200.0 },
    };

    auto reverb = [] (double decay, double predelay, double damping, double lowCut)
    {
        return std::vector<ParamSpec> {
            { "decay", "DECAY", 0.2, 10.0, decay, "s", {}, 1.5 },
            { "predelay", "PRE-DELAY", 0.0, 200.0, predelay, "ms", {}, 30.0 },
            { "damping", "HIGH CUT", 1000.0, 20000.0, damping, "Hz", {}, 6000.0 },
            { "lowCut", "LOW CUT", 20.0, 1000.0, lowCut, "Hz", {}, 150.0 },
            { "mix", "MIX", 0.0, 100.0, 30.0, "%", {}, 0 },
        };
    };

    static const std::vector<ParamSpec> room = reverb (0.6, 5.0, 8000.0, 150.0);
    static const std::vector<ParamSpec> hall = reverb (2.4, 25.0, 6000.0, 100.0);
    static const std::vector<ParamSpec> plate = reverb (1.8, 10.0, 11000.0, 150.0);

    switch (t)
    {
        case Type::busComp:     return busComp;
        case Type::saturator:   return saturator;
        case Type::roomReverb:  return room;
        case Type::hallReverb:  return hall;
        case Type::plateReverb: return plate;
        case Type::noiseGate:   return gate;
    }

    return busComp;
}

double paramValue (const nlohmann::json& params, const ParamSpec& spec)
{
    double v = spec.def;

    if (params.is_object())
        if (auto it = params.find (spec.key); it != params.end() && it->is_number())
            v = it->get<double>();

    v = std::clamp (v, spec.min, spec.max);
    return spec.choices.empty() ? v : std::round (v);
}

double paramValue (Type t, const nlohmann::json& params, const std::string& key)
{
    for (auto& s : paramSpecs (t))
        if (s.key == key)
            return paramValue (params, s);

    return 0.0;
}

const std::vector<Preset>& factoryPresets (Type t)
{
    using j = nlohmann::json;

    // バスコンプ: ratio 0/1/2 = 2/4/10、attack 0〜5 = 0.1/0.3/1/3/10/30 ms、release 0〜4 = 0.1/0.3/0.6/1.2/AUTO
    static const std::vector<Preset> busComp {
        { "ミックスバス（全体をまとめる）", j { { "threshold", -10.0 }, { "ratio", 0 }, { "attack", 5 }, { "release", 4 }, { "makeup", 1.5 }, { "sidechainHpf", 2 }, { "mix", 100.0 } } },
        { "ドラムバス（パンチ）", j { { "threshold", -18.0 }, { "ratio", 1 }, { "attack", 5 }, { "release", 0 }, { "makeup", 3.0 }, { "sidechainHpf", 1 }, { "mix", 100.0 } } },
        { "ボーカル（粒をそろえる）", j { { "threshold", -20.0 }, { "ratio", 1 }, { "attack", 3 }, { "release", 1 }, { "makeup", 4.0 }, { "sidechainHpf", 3 }, { "mix", 100.0 } } },
        { "パラレル（NY コンプ）", j { { "threshold", -32.0 }, { "ratio", 2 }, { "attack", 1 }, { "release", 0 }, { "makeup", 8.0 }, { "sidechainHpf", 0 }, { "mix", 40.0 } } },
    };

    static const std::vector<Preset> saturator {
        { "テープのように軽く", j { { "drive", 4.0 }, { "warmth", 60.0 }, { "output", 0.0 }, { "mix", 100.0 } } },
        { "ベースを太く", j { { "drive", 10.0 }, { "warmth", 85.0 }, { "output", 0.0 }, { "mix", 70.0 } } },
        { "ボーカルに艶", j { { "drive", 6.0 }, { "warmth", 30.0 }, { "output", 0.0 }, { "mix", 50.0 } } },
        { "しっかり歪ませる", j { { "drive", 18.0 }, { "warmth", 50.0 }, { "output", -3.0 }, { "mix", 100.0 } } },
    };

    static const std::vector<Preset> room {
        { "小さな部屋", j { { "decay", 0.4 }, { "predelay", 0.0 }, { "damping", 7000.0 }, { "lowCut", 200.0 } } },
        { "ドラムルーム", j { { "decay", 0.8 }, { "predelay", 2.0 }, { "damping", 8000.0 }, { "lowCut", 150.0 } } },
        { "ボーカルに空気感", j { { "decay", 0.6 }, { "predelay", 12.0 }, { "damping", 11000.0 }, { "lowCut", 300.0 } } },
    };

    static const std::vector<Preset> hall {
        { "ボーカル用ホール", j { { "decay", 2.2 }, { "predelay", 30.0 }, { "damping", 7000.0 }, { "lowCut", 200.0 } } },
        { "ストリングス・パッド", j { { "decay", 3.5 }, { "predelay", 20.0 }, { "damping", 5000.0 }, { "lowCut", 120.0 } } },
        { "大ホール", j { { "decay", 5.0 }, { "predelay", 40.0 }, { "damping", 4500.0 }, { "lowCut", 100.0 } } },
    };

    static const std::vector<Preset> plate {
        { "ボーカルプレート", j { { "decay", 1.6 }, { "predelay", 15.0 }, { "damping", 12000.0 }, { "lowCut", 200.0 } } },
        { "スネアプレート", j { { "decay", 1.2 }, { "predelay", 5.0 }, { "damping", 10000.0 }, { "lowCut", 250.0 } } },
        { "明るく長い", j { { "decay", 3.0 }, { "predelay", 20.0 }, { "damping", 15000.0 }, { "lowCut", 150.0 } } },
    };

    static const std::vector<Preset> gate {
        { "ボーカル（息・部屋の音を下げる）", j { { "threshold", -45.0 }, { "range", 12.0 }, { "attack", 2.0 }, { "hold", 80.0 }, { "release", 250.0 } } },
        { "ギター（アンプのノイズを切る）", j { { "threshold", -55.0 }, { "range", 80.0 }, { "attack", 1.0 }, { "hold", 50.0 }, { "release", 150.0 } } },
        { "ドラム（タムのかぶりを切る）", j { { "threshold", -30.0 }, { "range", 30.0 }, { "attack", 0.1 }, { "hold", 30.0 }, { "release", 80.0 } } },
    };

    switch (t)
    {
        case Type::busComp:     return busComp;
        case Type::saturator:   return saturator;
        case Type::roomReverb:  return room;
        case Type::hallReverb:  return hall;
        case Type::plateReverb: return plate;
        case Type::noiseGate:   return gate;
    }

    return busComp;
}

bool isReverb (Type t)
{
    return t == Type::roomReverb || t == Type::hallReverb || t == Type::plateReverb;
}

// 入力が止まっても音が続く長さ（秒）。再生（プラグインが報告する余韻）とバウンス・書き出しの両方でこれを使う
double tailSeconds (Type t, const nlohmann::json& params)
{
    if (isReverb (t))
        return paramValue (t, params, "decay") * 1.5 + paramValue (t, params, "predelay") / 1000.0;

    if (t == Type::busComp)
        return 1.5;   // リリースの戻り

    if (t == Type::noiseGate)
        return (paramValue (t, params, "hold") + paramValue (t, params, "release") * 3.0) / 1000.0;

    return 0.0;
}

nlohmann::json defaultParams (Type t, bool isBus)
{
    auto j = nlohmann::json::object();

    for (auto& s : paramSpecs (t))
        j[s.key] = s.def;

    if (isBus && isReverb (t))
        j["mix"] = 100.0;

    return j;
}

//==============================================================================
namespace
{
    /** バスコンプ: フィードフォワードの VCA。左右連動、ソフトニー、AUTO は 2 段のリリース（短い音には速く、長く続く圧縮には遅く）。 */
    class BusComp  : public Processor
    {
    public:
        void prepare (double sr) override       { sampleRate = sr; update(); reset(); }

        void reset() override
        {
            envFast = envSlow = 0.0;
            hpStates = {};
            gainReductionDb = 0.0f;
        }

        void setParams (const nlohmann::json& p) override
        {
            threshold = paramValue (Type::busComp, p, "threshold");
            ratio = std::array<double, 3> { 2.0, 4.0, 10.0 }[(size_t) paramValue (Type::busComp, p, "ratio")];
            attackMs = std::array<double, 6> { 0.1, 0.3, 1.0, 3.0, 10.0, 30.0 }[(size_t) paramValue (Type::busComp, p, "attack")];
            releaseIndex = (int) paramValue (Type::busComp, p, "release");
            makeup = paramValue (Type::busComp, p, "makeup");
            hpfIndex = (int) paramValue (Type::busComp, p, "sidechainHpf");
            mix = paramValue (Type::busComp, p, "mix") / 100.0;
            update();
        }

        void process (float* const* ch, int numChannels, int n) override
        {
            const double knee = 6.0;
            const double slope = 1.0 - 1.0 / ratio;
            const double makeupGain = dbToGain (makeup);

            for (int i = 0; i < n; ++i)
            {
                // 検出（左右の大きい方。サイドチェインの HPF で低域を無視できる）
                double det = 0.0;

                for (int c = 0; c < numChannels; ++c)
                {
                    const float x = hpfIndex > 0 ? hpStates[(size_t) c].process (hp, ch[c][i]) : ch[c][i];
                    det = std::max (det, (double) std::abs (x));
                }

                const double over = gainToDb (det) - threshold;
                double target = 0.0;

                if (over > knee * 0.5)
                    target = over * slope;
                else if (over > -knee * 0.5)
                    target = slope * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);

                // 平滑化（ゲインリダクションの dB で）
                if (releaseIndex == 4)
                {
                    envFast = target > envFast ? target + (envFast - target) * attackCoef : target + (envFast - target) * autoFastCoef;
                    envSlow = target > envSlow ? target + (envSlow - target) * autoSlowAttackCoef : target + (envSlow - target) * autoSlowCoef;
                }
                else
                {
                    envFast = target > envFast ? target + (envFast - target) * attackCoef : target + (envFast - target) * releaseCoef;
                    envSlow = 0.0;
                }

                const double gr = std::max (envFast, envSlow);
                const float gain = (float) (dbToGain (-gr) * makeupGain);

                for (int c = 0; c < numChannels; ++c)
                {
                    const float dry = ch[c][i];
                    ch[c][i] = (float) (dry * (1.0 - mix) + dry * gain * mix);
                }

                lastGr = gr;
            }

            gainReductionDb.store ((float) lastGr, std::memory_order_relaxed);
        }

    private:
        double sampleRate = 48000.0;
        double threshold = -12.0, ratio = 4.0, attackMs = 10.0, makeup = 0.0, mix = 1.0;
        int releaseIndex = 4, hpfIndex = 0;
        double attackCoef = 0, releaseCoef = 0, autoFastCoef = 0, autoSlowCoef = 0, autoSlowAttackCoef = 0;
        double envFast = 0, envSlow = 0, lastGr = 0;
        Biquad hp;
        std::array<BiquadState, 2> hpStates {};

        void update()
        {
            attackCoef = coefficient (attackMs / 1000.0, sampleRate);
            releaseCoef = coefficient (std::array<double, 4> { 0.1, 0.3, 0.6, 1.2 }[(size_t) std::min (releaseIndex, 3)], sampleRate);
            autoFastCoef = coefficient (0.1, sampleRate);
            autoSlowCoef = coefficient (1.5, sampleRate);
            autoSlowAttackCoef = coefficient (0.4, sampleRate);

            if (hpfIndex > 0)
                hp = Biquad::highPass (sampleRate, std::array<double, 4> { 0, 60, 90, 150 }[(size_t) hpfIndex], 0.707);
        }
    };

    //==============================================================================
    /**
        サチュレーター: 少し偏らせた tanh（偶数次の倍音が出る = 真空管・テープのような温かさ）を 2 倍オーバーサンプリングで。
        WARMTH で偏りと高域の丸さ・低域の厚みを増やす。DRIVE を上げた分の半分は自動で下げる。
    */
    class Saturator  : public Processor
    {
    public:
        Saturator()
        {
            // 2 倍オーバーサンプリング用のローパス（カットオフ = 元のナイキストの 0.9 倍、ブラックマン窓）
            const int mid = taps / 2;

            for (int k = 0; k < taps; ++k)
            {
                const double x = k - mid;
                const double cutoff = 0.45 * 0.5;   // 2 倍の速さでの正規化周波数
                const double sinc = x == 0 ? 2.0 * cutoff : std::sin (2.0 * pi * cutoff * x) / (pi * x);
                const double w = 0.42 - 0.5 * std::cos (2.0 * pi * k / (taps - 1)) + 0.08 * std::cos (4.0 * pi * k / (taps - 1));
                fir[(size_t) k] = sinc * w;
            }

            double sum = 0;
            for (auto v : fir) sum += v;
            for (auto& v : fir) v /= sum;
        }

        void prepare (double sr) override       { sampleRate = sr; update(); reset(); }

        void reset() override
        {
            for (auto& s : state)
                s = {};
        }

        void setParams (const nlohmann::json& p) override
        {
            drive = paramValue (Type::saturator, p, "drive");
            warmth = paramValue (Type::saturator, p, "warmth") / 100.0;
            output = paramValue (Type::saturator, p, "output");
            mix = paramValue (Type::saturator, p, "mix") / 100.0;
            update();
        }

        void process (float* const* ch, int numChannels, int n) override
        {
            const double in = dbToGain (drive);
            const double out = dbToGain (output - drive * 0.5);
            const double bias = 0.35 * warmth;
            const double tb = std::tanh (bias), norm = 1.0 - tb * tb;
            const double dcCoef = 1.0 - 2.0 * pi * 10.0 / sampleRate;

            for (int c = 0; c < std::min (numChannels, 2); ++c)
            {
                auto& s = state[(size_t) c];

                for (int i = 0; i < n; ++i)
                {
                    const float dry = ch[c][i];

                    // 2 倍に: 0 を挟んでローパス（振幅を合わせるため 2 倍）
                    double up[2];

                    for (int k = 0; k < 2; ++k)
                    {
                        s.upHistory[(size_t) s.upPos] = k == 0 ? 2.0 * dry * in : 0.0;
                        s.upPos = (s.upPos + 1) % taps;
                        up[k] = convolve (s.upHistory, s.upPos);
                    }

                    // 歪み（偏った tanh。小さな音の傾きは 1 になるよう割る）と、2 倍のまま帯域を制限して間引く
                    double down = 0;

                    for (int k = 0; k < 2; ++k)
                    {
                        const double y = (std::tanh (up[k] + bias) - tb) / norm;
                        s.downHistory[(size_t) s.downPos] = y;
                        s.downPos = (s.downPos + 1) % taps;

                        if (k == 1)
                            down = convolve (s.downHistory, s.downPos);
                    }

                    // 直流を取る（偏りで出る）
                    const double blocked = down - s.dcX + dcCoef * s.dcY;
                    s.dcX = down;
                    s.dcY = blocked;

                    // 温かさ: 高域を少し丸め、低域を少し厚く
                    float v = (float) blocked;
                    v = s.shelfHi.process (highShelf, v);
                    v = s.shelfLo.process (lowShelf, v);
                    ch[c][i] = (float) (dry * (1.0 - mix) + v * out * mix);
                }
            }
        }

    private:
        static constexpr int taps = 31;
        std::array<double, taps> fir {};
        double sampleRate = 48000.0, drive = 6.0, warmth = 0.5, output = 0.0, mix = 1.0;
        Biquad highShelf, lowShelf;

        struct State
        {
            std::array<double, taps> upHistory {}, downHistory {};
            int upPos = 0, downPos = 0;
            double dcX = 0, dcY = 0;
            BiquadState shelfHi, shelfLo;
        };

        std::array<State, 2> state;

        double convolve (const std::array<double, taps>& history, int pos) const noexcept
        {
            double acc = 0;

            for (int k = 0; k < taps; ++k)
                acc += fir[(size_t) k] * history[(size_t) ((pos + taps - 1 - k) % taps)];

            return acc;
        }

        void update()
        {
            highShelf = Biquad::highShelf (sampleRate, 7000.0, -6.0 * warmth);
            lowShelf = Biquad::lowShelf (sampleRate, 120.0, 2.0 * warmth);
        }
    };

    //==============================================================================
    /**
        リバーブ: 8 本の遅延線のフィードバック遅延ネットワーク（ハウスホルダー行列）。前に全域通過フィルタで音を散らし、
        各線に高域の減衰（HIGH CUT）を入れる。ホールとプレートは遅延を少し揺らして金属的な響きを避ける。
        ルームは短い線と初期反射、ホールは長い線、プレートは密な拡散で初期反射なし。
    */
    class Reverb  : public Processor
    {
    public:
        explicit Reverb (Type t) : type (t) {}

        void prepare (double sr) override       { sampleRate = sr; build(); reset(); }

        void reset() override
        {
            predelay.clear();

            for (auto& l : lines) l.clear();
            for (auto& a : diffusers) a.clear();

            lineState.fill (0.0f);
            lowCutStates = {};
            phase = 0.0;
        }

        void setParams (const nlohmann::json& p) override
        {
            decay = paramValue (type, p, "decay");
            predelayMs = paramValue (type, p, "predelay");
            damping = paramValue (type, p, "damping");
            lowCut = paramValue (type, p, "lowCut");
            mix = paramValue (type, p, "mix") / 100.0;
            update();
        }


        void process (float* const* ch, int numChannels, int n) override
        {
            const double predelaySamples = std::max (1.0, predelayMs / 1000.0 * sampleRate);
            const double lfoStep = 2.0 * pi * 0.7 / sampleRate;

            for (int i = 0; i < n; ++i)
            {
                const float l = ch[0][i], r = numChannels > 1 ? ch[1][i] : l;
                float x = 0.5f * (l + r);

                // 低域を切る（響きがこもらないように）
                x = lowCutStates[0].process (lowCutCoeffs, x);
                predelay.push (x);
                float in = predelay.read (predelaySamples);

                // 初期反射（ルーム・ホール）
                float erL = 0.0f, erR = 0.0f;

                for (size_t k = 0; k < earlyTaps.size(); ++k)
                {
                    const float v = predelay.read (predelaySamples + earlyTaps[k]) * earlyGains[k];
                    (k % 2 == 0 ? erL : erR) += v;
                }

                for (auto& a : diffusers)
                    in = a.process (in);

                // 遅延線の出力 → 高域の減衰 → ハウスホルダー行列 → 入力を足して書き戻す
                std::array<float, numLines> out {};
                float sum = 0.0f;

                for (int k = 0; k < numLines; ++k)
                {
                    double d = lineLengths[(size_t) k];

                    if (modDepth > 0.0 && k < 4)
                        d += modDepth * std::sin (phase + k * 1.7);

                    float v = lines[(size_t) k].read (d) * lineGains[(size_t) k];
                    lineState[(size_t) k] = v + (lineState[(size_t) k] - v) * dampCoef;   // 1 次のローパス
                    out[(size_t) k] = lineState[(size_t) k];
                    sum += out[(size_t) k];
                }

                const float house = sum * (2.0f / numLines);

                for (int k = 0; k < numLines; ++k)
                    lines[(size_t) k].push (out[(size_t) k] - house + in * inputGain);

                phase += lfoStep;

                if (phase > 2.0 * pi)
                    phase -= 2.0 * pi;

                // 出力: 線ごとに符号を変えて左右を作る（広がり）
                float wetL = 0.0f, wetR = 0.0f;

                for (int k = 0; k < numLines; ++k)
                {
                    wetL += out[(size_t) k] * ((k & 1) ? -1.0f : 1.0f);
                    wetR += out[(size_t) k] * ((k & 2) ? -1.0f : 1.0f);
                }

                wetL = wetL * outputGain + erL;
                wetR = wetR * outputGain + erR;

                const float dryGain = (float) (1.0 - mix), wetGain = (float) mix;
                ch[0][i] = l * dryGain + wetL * wetGain;

                if (numChannels > 1)
                    ch[1][i] = r * dryGain + wetR * wetGain;
                else
                    ch[0][i] = l * dryGain + 0.5f * (wetL + wetR) * wetGain;
            }
        }

    private:
        static constexpr int numLines = 8;
        Type type;
        double sampleRate = 48000.0;
        double decay = 2.0, predelayMs = 20.0, damping = 8000.0, lowCut = 100.0, mix = 0.3;

        DelayLine predelay;
        std::array<DelayLine, numLines> lines;
        std::array<double, numLines> lineLengths {};
        std::array<float, numLines> lineGains {}, lineState {};
        std::vector<Allpass> diffusers;
        std::vector<double> earlyTaps;
        std::vector<float> earlyGains;
        float dampCoef = 0.0f, inputGain = 0.35f, outputGain = 0.3f;
        double modDepth = 0.0, phase = 0.0;
        Biquad lowCutCoeffs;
        std::array<BiquadState, 1> lowCutStates {};

        void build()
        {
            // 遅延線の長さ（ms）。互いに素に近い長さで、響きが均一になるように
            static constexpr std::array<double, numLines> base { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1 };
            const double scale = type == Type::roomReverb ? 0.32 : type == Type::plateReverb ? 0.55 : 1.15;

            for (int k = 0; k < numLines; ++k)
            {
                lineLengths[(size_t) k] = base[(size_t) k] * scale / 1000.0 * sampleRate;
                lines[(size_t) k].setMaxSamples ((int) lineLengths[(size_t) k] + 64);
            }

            predelay.setMaxSamples ((int) (0.35 * sampleRate));

            // 入口の拡散（Dattorro のプレートと同じ長さの比）
            const std::array<double, 4> apMs { 4.771, 3.595, 12.73, 9.307 };
            const float apGain = type == Type::plateReverb ? 0.72f : 0.55f;
            diffusers.assign (4, {});

            for (size_t k = 0; k < 4; ++k)
                diffusers[k].setup ((int) (apMs[k] / 1000.0 * sampleRate), k < 2 ? apGain : apGain * 0.85f);

            // 初期反射
            earlyTaps.clear();
            earlyGains.clear();

            if (type == Type::roomReverb)
            {
                const std::array<double, 8> ms { 4.3, 7.9, 11.2, 14.9, 19.1, 23.3, 28.7, 34.1 };

                for (size_t k = 0; k < ms.size(); ++k)
                {
                    earlyTaps.push_back (ms[k] / 1000.0 * sampleRate);
                    earlyGains.push_back ((float) (0.32 * std::pow (0.8, (double) k)));
                }
            }
            else if (type == Type::hallReverb)
            {
                const std::array<double, 6> ms { 17.0, 26.0, 35.0, 47.0, 61.0, 79.0 };

                for (size_t k = 0; k < ms.size(); ++k)
                {
                    earlyTaps.push_back (ms[k] / 1000.0 * sampleRate);
                    earlyGains.push_back ((float) (0.14 * std::pow (0.82, (double) k)));
                }
            }

            modDepth = type == Type::roomReverb ? 0.0 : (type == Type::hallReverb ? 0.0006 : 0.0003) * sampleRate;
            outputGain = type == Type::roomReverb ? 0.6f : 0.55f;   // MIX 100% でも元の音と同じくらいの大きさに近づける
            update();
        }

        void update()
        {
            // 残響時間: 1 周するごとに 60 dB × (線の長さ / 残響時間) だけ下げる
            for (int k = 0; k < numLines; ++k)
                lineGains[(size_t) k] = (float) std::pow (10.0, -3.0 * lineLengths[(size_t) k] / (decay * sampleRate));

            dampCoef = (float) std::exp (-2.0 * pi * damping / sampleRate);
            lowCutCoeffs = Biquad::highPass (sampleRate, lowCut, 0.707);
        }
    };
}

namespace
{
    /**
        ノイズゲート: 左右連動のピーク検出。スレッショルドを超えたら開き（ATTACK で上げる）、
        スレッショルドより 4 dB 下（ヒステリシス）を下回ってから HOLD だけ待って閉じる（RELEASE で RANGE まで下げる）。
        ゲインは dB で動かす（閉じるときの消え方が自然になる）。
    */
    class NoiseGate  : public Processor
    {
    public:
        void prepare (double sr) override       { sampleRate = sr; update(); reset(); }

        void reset() override
        {
            gainDb = -range;
            open = false;
            holdLeft = 0;
            peak = 0.0;
            gainReductionDb = 0.0f;
        }

        void setParams (const nlohmann::json& p) override
        {
            threshold = paramValue (Type::noiseGate, p, "threshold");
            range = paramValue (Type::noiseGate, p, "range");
            attackMs = paramValue (Type::noiseGate, p, "attack");
            holdMs = paramValue (Type::noiseGate, p, "hold");
            releaseMs = paramValue (Type::noiseGate, p, "release");
            update();
        }

        void process (float* const* ch, int numChannels, int n) override
        {
            const double openLevel = std::pow (10.0, threshold / 20.0);
            const double closeLevel = std::pow (10.0, (threshold - 4.0) / 20.0);
            double maxReduction = 0.0;

            for (int i = 0; i < n; ++i)
            {
                double level = 0.0;

                for (int c = 0; c < numChannels; ++c)
                    level = std::max (level, (double) std::abs (ch[c][i]));

                // ピーク: すぐ上がり、10 ms ほどで下がる（1 周期の中で開け閉めしない）
                peak = std::max (level, peak * peakDecay);

                if (peak >= openLevel)
                {
                    open = true;
                    holdLeft = holdSamples;
                }
                else if (open && peak < closeLevel)
                {
                    if (holdLeft > 0)
                        --holdLeft;
                    else
                        open = false;
                }

                const double target = open ? 0.0 : -range;
                gainDb += (target - gainDb) * (target > gainDb ? attackCoef : releaseCoef);
                const auto g = (float) std::pow (10.0, gainDb / 20.0);

                for (int c = 0; c < numChannels; ++c)
                    ch[c][i] *= g;

                maxReduction = std::max (maxReduction, -gainDb);
            }

            gainReductionDb.store ((float) maxReduction, std::memory_order_relaxed);
        }

    private:
        double sampleRate = 48000.0;
        double threshold = -50.0, range = 80.0, attackMs = 1.0, holdMs = 50.0, releaseMs = 150.0;
        double attackCoef = 1.0, releaseCoef = 1.0, peakDecay = 0.99, peak = 0.0, gainDb = -80.0;
        int holdSamples = 0, holdLeft = 0;
        bool open = false;

        void update()
        {
            // 時定数の約 5 倍で目標に届く（ATTACK・RELEASE はほぼ開き切る・閉じ切るまでの時間）
            auto coef = [this] (double ms) { return 1.0 - std::exp (-5.0 / (std::max (0.05, ms) * 0.001 * sampleRate)); };
            attackCoef = coef (attackMs);
            releaseCoef = coef (releaseMs);
            holdSamples = (int) (holdMs * 0.001 * sampleRate);
            peakDecay = std::exp (-1.0 / (0.010 * sampleRate));
        }
    };
}

std::unique_ptr<Processor> createProcessor (Type t)
{
    switch (t)
    {
        case Type::noiseGate:   return std::make_unique<NoiseGate>();
        case Type::busComp:     return std::make_unique<BusComp>();
        case Type::saturator:   return std::make_unique<Saturator>();
        case Type::roomReverb:
        case Type::hallReverb:
        case Type::plateReverb: return std::make_unique<Reverb> (t);
    }

    return {};
}

}
