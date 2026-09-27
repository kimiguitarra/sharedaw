#include "collab/chord/Degree.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace collab::chord
{

namespace
{
    constexpr std::array<int, 7> majorScale { 0, 2, 4, 5, 7, 9, 11 };
    constexpr std::array<int, 7> minorScale { 0, 2, 3, 5, 7, 8, 10 };   // ナチュラルマイナー

    // 音階の三和音（1〜7 度）
    const std::array<const char*, 7> majorTriads { "", "m", "m", "", "", "m", "dim" };
    const std::array<const char*, 7> minorTriads { "m", "dim", "", "m", "m", "", "" };

    const std::array<const char*, 7> numerals { "Ⅰ", "Ⅱ", "Ⅲ", "Ⅳ", "Ⅴ", "Ⅵ", "Ⅶ" };

    const std::array<const char*, 12> sharpNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const std::array<const char*, 12> flatNames  { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    const std::array<const char*, 12> mixedNames { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

    int mod12 (int v)     { return ((v % 12) + 12) % 12; }

    const std::array<int, 7>& scaleOf (const Key& k)  { return k.minor ? minorScale : majorScale; }

    /** 調号がフラット系なら -1、シャープ系なら +1、C / Am は 0。 */
    int signatureDirection (const Key& k)
    {
        const int major = k.minor ? mod12 (k.tonic + 3) : k.tonic;   // 平行調のメジャーで判断する

        switch (major)
        {
            case 5: case 10: case 3: case 8: case 1: case 6:  return -1;   // F Bb Eb Ab Db Gb
            case 7: case 2: case 9: case 4: case 11:          return 1;    // G D A E B
            default:                                          return 0;
        }
    }

    /** ルートから見た音程（0..11）をディグリーの表記に（音階の音はそのまま、外れた音は ♭ / ♯）。 */
    std::string numeralFor (int interval, const Key& k)
    {
        const auto& scale = scaleOf (k);

        for (int d = 0; d < 7; ++d)
            if (scale[(size_t) d] == interval)
                return numerals[(size_t) d];

        // 音階の音の半音上か下。メジャーは ♭ を優先（♯Ⅳ だけは ♯）、
        // マイナーは ♯ を優先（♯Ⅵ・♯Ⅶ は旋律的・和声的短音階の音。♭Ⅱ だけは ♭）
        int flatOf = -1, sharpOf = -1;

        for (int d = 0; d < 7; ++d)
        {
            if (mod12 (scale[(size_t) d] - 1) == interval && d != 0) flatOf = d;
            if (mod12 (scale[(size_t) d] + 1) == interval) sharpOf = d;
        }

        const bool useSharp = k.minor ? (sharpOf >= 0 && flatOf != 1) : (sharpOf == 3 || flatOf < 0);

        if (useSharp && sharpOf >= 0)
            return std::string ("♯") + numerals[(size_t) sharpOf];

        return std::string ("♭") + numerals[(size_t) flatOf];
    }

    /** 全角の数字・記号を半角に。 */
    std::string normalise (const std::string& s)
    {
        std::string out;

        for (size_t i = 0; i < s.size();)
        {
            const auto c = (unsigned char) s[i];

            // 全角数字 ０〜９（EF BC 90〜99）
            if (c == 0xEF && i + 2 < s.size() && (unsigned char) s[i + 1] == 0xBC
                  && (unsigned char) s[i + 2] >= 0x90 && (unsigned char) s[i + 2] <= 0x99)
            {
                out += (char) ('0' + ((unsigned char) s[i + 2] - 0x90));
                i += 3;
                continue;
            }

            // 全角スラッシュ ／（EF BC 8F）
            if (c == 0xEF && i + 2 < s.size() && (unsigned char) s[i + 1] == 0xBC && (unsigned char) s[i + 2] == 0x8F)
            {
                out += '/';
                i += 3;
                continue;
            }

            if (c != ' ' && c != '\t')
                out += s[i];

            ++i;
        }

        return out;
    }

    bool startsWith (const std::string& s, size_t pos, const std::string& prefix)
    {
        return s.compare (pos, prefix.size(), prefix) == 0;
    }

    struct Degree
    {
        int degree = 0;          // 0..6
        int accidental = 0;      // -1 / 0 / +1
        bool lowercase = false;  // 小文字のローマ数字（マイナー）
        size_t length = 0;
    };

    /** pos からディグリー（臨時記号 + 数字 / ローマ数字）を読む。 */
    std::optional<Degree> readDegree (const std::string& s, size_t pos)
    {
        Degree d;
        size_t i = pos;

        if (startsWith (s, i, "♭"))      { d.accidental = -1; i += std::string ("♭").size(); }
        else if (startsWith (s, i, "♯")) { d.accidental = 1;  i += std::string ("♯").size(); }
        else if (i < s.size() && (s[i] == 'b' || s[i] == '#'))
        {
            d.accidental = s[i] == 'b' ? -1 : 1;
            ++i;
        }

        if (i < s.size() && s[i] >= '1' && s[i] <= '7')
        {
            d.degree = s[i] - '1';
            d.length = i + 1 - pos;
            return d;
        }

        // Unicode のローマ数字 Ⅰ〜Ⅶ（長い順に照合する必要はない: 1 文字ずつ別の文字）
        for (int n = 0; n < 7; ++n)
            if (startsWith (s, i, numerals[(size_t) n]))
            {
                d.degree = n;
                d.length = i + std::string (numerals[(size_t) n]).size() - pos;
                return d;
            }

        // ASCII のローマ数字（長いものから）
        static const std::vector<std::pair<std::string, int>> romans {
            { "VII", 6 }, { "III", 2 }, { "VI", 5 }, { "IV", 3 }, { "II", 1 }, { "V", 4 }, { "I", 0 }
        };

        for (auto& [text, n] : romans)
        {
            std::string lower = text;
            for (auto& ch : lower)
                ch = (char) std::tolower ((unsigned char) ch);

            for (bool isLower : { false, true })
                if (startsWith (s, i, isLower ? lower : text))
                {
                    d.degree = n;
                    d.lowercase = isLower;
                    d.length = i + text.size() - pos;
                    return d;
                }
        }

        return std::nullopt;
    }

    int intervalOf (const Degree& d, const Key& k)
    {
        return mod12 (scaleOf (k)[(size_t) d.degree] + d.accidental);
    }
}

//==============================================================================
std::string keyName (const Key& k)
{
    return spellPitch (k.tonic, k) + (k.minor ? "m" : "");
}

std::string spellPitch (int pc, const Key& k)
{
    pc = mod12 (pc);
    const int dir = signatureDirection (k);
    return dir < 0 ? flatNames[(size_t) pc] : dir > 0 ? sharpNames[(size_t) pc] : mixedNames[(size_t) pc];
}

std::string degreeName (const Chord& c, const Key& k)
{
    const int root = pitchClass (c.root);

    if (root < 0)
        return {};

    // コードタイプ・テンションの部分は通常の表記と同じにする（ルートとベースを除いたもの）
    Chord body = c;
    body.bass.reset();
    auto text = format (body);
    const auto suffix = text.substr (c.root.size());

    auto s = numeralFor (mod12 (root - k.tonic), k) + suffix;

    if (c.bass)
        if (const int bass = pitchClass (*c.bass); bass >= 0)
            s += "/" + numeralFor (mod12 (bass - k.tonic), k);

    return s;
}

std::optional<std::string> degreeToChordText (const std::string& input, const Key& k)
{
    const auto s = normalise (input);

    if (s.empty())
        return std::nullopt;

    auto d = readDegree (s, 0);

    if (! d)
        return std::nullopt;

    // 残り: コードタイプ・テンション（あれば）と、/ベース
    auto rest = s.substr (d->length);
    std::string bassText;

    if (auto slash = rest.find ('/'); slash != std::string::npos)
    {
        const auto bassPart = rest.substr (slash + 1);
        rest = rest.substr (0, slash);

        if (auto b = readDegree (bassPart, 0); b && b->length == bassPart.size())
            bassText = "/" + spellPitch (k.tonic + intervalOf (*b, k), k);
        else if (! bassPart.empty() && pitchClass (bassPart) >= 0)
            bassText = "/" + bassPart;
        else
            return std::nullopt;
    }

    // 後ろがコードタイプとして読めないもの（例: "12"）はディグリーとみなさない
    const auto rootName = spellPitch (k.tonic + intervalOf (*d, k), k);
    std::string quality;

    if (rest.empty())
    {
        // 何も付けなければ音階の三和音（臨時記号付きはメジャー、小文字のローマ数字はマイナー）
        if (d->lowercase)
            quality = "m";
        else if (d->accidental == 0)
            quality = (k.minor ? minorTriads : majorTriads)[(size_t) d->degree];
    }
    else
    {
        // 小文字のローマ数字に 7 などが付いたらマイナー（vi7 → Am7）
        quality = d->lowercase && std::isdigit ((unsigned char) rest[0]) ? "m" + rest : rest;
    }

    auto text = rootName + quality + bassText;

    if (! parse (text).chord)
        return std::nullopt;

    return text;
}

std::optional<Key> estimateKey (const std::vector<Chord>& chords)
{
    std::vector<std::pair<int, std::vector<int>>> tones;   // ルートと構成音（ピッチクラス）

    for (auto& c : chords)
    {
        const int root = pitchClass (c.root);

        if (root < 0)
            continue;

        std::vector<int> pcs;

        for (int i : chordIntervals (c))
            pcs.push_back (mod12 (root + i));

        tones.emplace_back (root, pcs);
    }

    if (tones.empty())
        return std::nullopt;

    std::optional<Key> best;
    double bestScore = -1e9;

    for (int minor = 0; minor < 2; ++minor)
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            const Key k { tonic, minor == 1 };
            const auto& scale = scaleOf (k);
            auto inScale = [&] (int pc) { return std::find (scale.begin(), scale.end(), mod12 (pc - tonic)) != scale.end(); };
            double score = 0.0;

            for (auto& [root, pcs] : tones)
            {
                score += inScale (root) ? 2.0 : -2.0;

                for (int pc : pcs)
                    score += inScale (pc) ? 1.0 : -1.0;

                // マイナーキーの Ⅴ（和声的短音階の導音を含む）は外れ扱いしない
                if (k.minor && mod12 (root - tonic) == 7)
                    score += 2.0;
            }

            // 主和音で始まる・終わると、そのキーらしい
            auto isTonic = [&] (const std::pair<int, std::vector<int>>& t)
            {
                if (t.first != tonic)
                    return false;

                const bool hasMinorThird = std::find (t.second.begin(), t.second.end(), mod12 (tonic + 3)) != t.second.end();
                return hasMinorThird == k.minor;
            };

            if (isTonic (tones.front())) score += 3.0;
            if (isTonic (tones.back()))  score += 4.0;

            // 同点ならメジャーを優先（平行調はどちらとも取れるため）
            if (score > bestScore + (k.minor ? 0.5 : 0.0))
            {
                bestScore = score;
                best = k;
            }
        }

    return best;
}

} // namespace collab::chord
