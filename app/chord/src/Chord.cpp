#include "collab/chord/Chord.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <numeric>
#include <set>

namespace collab::chord
{

namespace
{
    const std::vector<std::string> kQualities = { "maj", "m", "dim", "aug", "sus2", "sus4",
                                                  "6", "m6", "7", "maj7", "m7", "mM7", "m7b5", "dim7", "7sus4",
                                                  "add9" };

    const std::vector<std::string> kTensions = { "b9", "9", "#9", "11", "#11", "b13", "13" };

    bool startsWith (const std::string& s, size_t pos, const std::string& prefix)
    {
        return s.compare (pos, prefix.size(), prefix) == 0;
    }

    void replaceAll (std::string& s, const std::string& from, const std::string& to)
    {
        for (size_t pos = 0; (pos = s.find (from, pos)) != std::string::npos; pos += to.size())
            s.replace (pos, from.size(), to);
    }

    /** 表記の揺れ（記号・全角・空白）をそろえる。 */
    std::string normalise (std::string s)
    {
        // 全角・記号
        const std::pair<const char*, const char*> table[] = {
            { "\xE2\x99\xAF", "#" },   // ♯
            { "\xE2\x99\xAD", "b" },   // ♭
            { "\xEF\xBC\x83", "#" },   // ＃
            { "\xE2\x96\xB3", "^" },   // △
            { "\xCE\x94", "^" },       // Δ
            { "\xE2\x88\x86", "^" },   // ∆
            { "\xC3\xB8", "@" },       // ø
            { "\xC3\x98", "@" },       // Ø
            { "\xC2\xB0", "o" },       // °
            { "\xEF\xBC\x88", "(" },   // （
            { "\xEF\xBC\x89", ")" },   // ）
            { "\xEF\xBC\x8C", "," },   // ，
            { "\xE3\x80\x81", "," },   // 、
            { "\xEF\xBC\x8F", "/" },   // ／
            { "\xEF\xBC\x8B", "+" },   // ＋
            { "\xE2\x88\x92", "-" },   // −
            { "\xEF\xBC\x8D", "-" },   // －
            { "\xE3\x80\x80", " " },   // 全角スペース
        };

        for (auto& [from, to] : table)
            replaceAll (s, from, to);

        // 全角英数字 → 半角（U+FF10〜FF19, FF21〜FF3A, FF41〜FF5A: EF BC 90〜 / EF BD 81〜）
        std::string out;

        for (size_t i = 0; i < s.size(); ++i)
        {
            const auto c = (unsigned char) s[i];

            if (c == 0xEF && i + 2 < s.size())
            {
                const auto b1 = (unsigned char) s[i + 1], b2 = (unsigned char) s[i + 2];
                const int cp = 0xF000 | ((b1 & 0x3F) << 6) | (b2 & 0x3F);   // EF xx xx → U+Fxxx

                if (cp >= 0xFF01 && cp <= 0xFF5E)
                {
                    out += (char) (cp - 0xFF01 + 0x21);
                    i += 2;
                    continue;
                }
            }

            if (c != ' ' && c != '\t')
                out += (char) c;
        }

        return out;
    }

    std::string normaliseTension (std::string t)
    {
        if (! t.empty() && (t[0] == '-'))  t[0] = 'b';
        if (! t.empty() && (t[0] == '+'))  t[0] = '#';

        if (std::find (kTensions.begin(), kTensions.end(), t) != kTensions.end())
            return t;

        return {};
    }

    void addTension (std::vector<std::string>& tensions, const std::string& t)
    {
        if (std::find (tensions.begin(), tensions.end(), t) == tensions.end())
            tensions.push_back (t);
    }

    void sortTensions (std::vector<std::string>& tensions)
    {
        std::sort (tensions.begin(), tensions.end(), [] (auto& a, auto& b)
        {
            auto ia = std::find (kTensions.begin(), kTensions.end(), a) - kTensions.begin();
            auto ib = std::find (kTensions.begin(), kTensions.end(), b) - kTensions.begin();
            return ia < ib;
        });
    }

    /** 音名（A〜G + #/b）を読む。読めた文字数を返す（0 なら失敗）。 */
    size_t readNoteName (const std::string& s, size_t pos, std::string& out)
    {
        if (pos >= s.size() || s[pos] < 'A' || s[pos] > 'G')
            return 0;

        out = s.substr (pos, 1);

        if (pos + 1 < s.size() && (s[pos + 1] == '#' || s[pos + 1] == 'b'))
        {
            out += s[pos + 1];
            return 2;
        }

        return 1;
    }

    ParseResult fail (const std::string& message)
    {
        ParseResult r;
        r.error = message;
        return r;
    }
}

const std::vector<std::string>& allQualities()   { return kQualities; }
const std::vector<std::string>& allTensions()    { return kTensions; }

int pitchClass (const std::string& name)
{
    if (name.empty() || name[0] < 'A' || name[0] > 'G')
        return -1;

    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    int pc = base[name[0] - 'A'];

    for (size_t i = 1; i < name.size(); ++i)
    {
        if (name[i] == '#')       ++pc;
        else if (name[i] == 'b')  --pc;
        else return -1;
    }

    return ((pc % 12) + 12) % 12;
}

//==============================================================================
ParseResult parse (const std::string& input)
{
    std::string s = normalise (input);

    if (s.empty())
        return fail ("コードが入力されていません");

    if (s == "X" || s == "x" || s == "N.C." || s == "NC" || s == "N.C" || s == "n.c.")
    {
        ParseResult r;
        r.noChord = true;
        return r;
    }

    Chord chord;
    size_t pos = readNoteName (s, 0, chord.root);

    if (pos == 0)
        return fail ("ルート音（C〜B）が読めません");

    std::string rest = s.substr (pos);

    // 6/9 は分数コードではなく 6(9)
    replaceAll (rest, "6/9", "6(9)");

    // 分数コードのベース音
    if (auto slash = rest.rfind ('/'); slash != std::string::npos)
    {
        std::string bass;
        const auto n = readNoteName (rest, slash + 1, bass);

        if (n == 0 || slash + 1 + n != rest.size())
            return fail ("分数コードのベース音が読めません");

        chord.bass = bass;
        rest = rest.substr (0, slash);
    }

    // m(maj7) などの括弧はコードタイプの一部として扱う
    for (auto* q : { "(maj7)", "(Maj7)", "(M7)", "(^7)", "(^)" })
        replaceAll (rest, q, std::string (q).substr (1, std::string (q).size() - 2));

    // 括弧の中のテンション（b5 はコードタイプの一部）
    std::vector<std::string> tensions;
    bool flatFive = false;

    for (size_t open; (open = rest.find ('(')) != std::string::npos;)
    {
        const auto close = rest.find (')', open);

        if (close == std::string::npos)
            return fail ("括弧が閉じていません");

        std::string inner = rest.substr (open + 1, close - open - 1);
        rest.erase (open, close - open + 1);
        replaceAll (inner, " ", "");

        size_t start = 0;

        while (start <= inner.size())
        {
            auto comma = inner.find (',', start);
            auto token = inner.substr (start, comma == std::string::npos ? std::string::npos : comma - start);

            if (! token.empty())
            {
                if (token == "b5" || token == "-5")
                    flatFive = true;
                else if (auto t = normaliseTension (token); ! t.empty())
                    addTension (tensions, t);
                else if (token.rfind ("add", 0) == 0 && ! normaliseTension (token.substr (3)).empty())
                    addTension (tensions, normaliseTension (token.substr (3)));
                else
                    return fail ("テンション「" + token + "」は対応していません");
            }

            if (comma == std::string::npos)
                break;

            start = comma + 1;
        }
    }

    // --- コードタイプ ---
    size_t p = 0;
    bool minor = false, majorMarker = false, dim = false, aug = false, halfDim = false;
    bool sus2 = false, sus4 = false, add9 = false;
    int number = 0;

    auto consume = [&] (const char* token)
    {
        if (startsWith (rest, p, token))
        {
            p += std::string (token).size();
            return true;
        }
        return false;
    };

    auto consumeMajorMarker = [&]
    {
        if (consume ("maj") || consume ("Maj") || consume ("MAJ") || consume ("M") || consume ("^"))
            return true;

        // "ma7" は許すが "madd9" の "ma" は major ではない
        if (startsWith (rest, p, "ma") && p + 2 < rest.size() && std::isdigit ((unsigned char) rest[p + 2]))
        {
            p += 2;
            return true;
        }

        return false;
    };

    if (consumeMajorMarker())                                        majorMarker = true;
    else if (consume ("min") || consume ("mi") || consume ("m") || consume ("-"))
    {
        minor = true;
        if (consumeMajorMarker())
            majorMarker = true;
    }
    else if (consume ("dim") || consume ("o"))                       dim = true;
    else if (consume ("aug") || consume ("+"))                       aug = true;
    else if (consume ("@"))                                          halfDim = true;

    if (consume ("69"))        { number = 6; addTension (tensions, "9"); }
    else if (consume ("13"))   number = 13;
    else if (consume ("11"))   number = 11;
    else if (consume ("9"))    number = 9;
    else if (consume ("7"))    number = 7;
    else if (consume ("6"))    number = 6;

    // 後ろに続く修飾（sus, add9, b5, 連結テンション）
    while (p < rest.size())
    {
        if (consume ("sus4"))                          sus4 = true;
        else if (consume ("sus2"))                     sus2 = true;
        else if (consume ("sus"))                      sus4 = true;
        else if (consume ("add9") || consume ("add2")) add9 = true;
        else if (consume ("b5") || consume ("-5"))     flatFive = true;
        else if (consume ("b9") || consume ("-9"))     addTension (tensions, "b9");
        else if (consume ("#9") || consume ("+9"))     addTension (tensions, "#9");
        else if (consume ("#11") || consume ("+11"))   addTension (tensions, "#11");
        else if (consume ("b13") || consume ("-13"))   addTension (tensions, "b13");
        else if (consume ("13"))                       addTension (tensions, "13");
        else if (consume ("11"))                       addTension (tensions, "11");
        else if (consume ("9"))                        addTension (tensions, "9");
        else
            return fail ("「" + rest.substr (p) + "」が読めません");
    }

    // 9 / 11 / 13 は 7 の和音 + テンション（11 は 9 を、13 は 9 を含む）
    bool seventh = number >= 7;

    if (number == 9)   addTension (tensions, "9");
    if (number == 11)  { addTension (tensions, "9"); addTension (tensions, "11"); }
    if (number == 13)  { addTension (tensions, "9"); addTension (tensions, "13"); }

    std::string q;

    if (halfDim)
    {
        if (number != 0 && number != 7)
            return fail ("ø の後ろには 7 だけを書けます");
        q = "m7b5";
    }
    else if (dim)
    {
        q = number == 7 ? "dim7" : number == 0 ? "dim" : "";
        if (q.empty()) return fail ("dim には 7 以外の数字は付けられません");
    }
    else if (aug)
    {
        if (number != 0) return fail ("aug7 などは対応していません（aug のみ）");
        q = "aug";
    }
    else if (sus4 || sus2)
    {
        if (minor || majorMarker)  return fail ("sus には m / M を付けられません");
        if (sus2 && seventh)       return fail ("7sus2 は対応していません");
        q = sus2 ? "sus2" : seventh ? "7sus4" : "sus4";
    }
    else if (minor)
    {
        if (flatFive)
        {
            if (number != 7 || majorMarker) return fail ("b5 は m7(b5) の形だけ対応しています");
            q = "m7b5";
            flatFive = false;
        }
        else if (majorMarker)  q = seventh ? "mM7" : "";
        else if (number == 6)  q = "m6";
        else if (seventh)      q = "m7";
        else                   q = "m";

        if (q.empty()) return fail ("mM の後ろには 7 などの数字が必要です");
    }
    else if (majorMarker)
    {
        q = number == 6 ? "6" : seventh ? "maj7" : "maj";
    }
    else
    {
        q = number == 6 ? "6" : seventh ? "7" : "maj";
    }

    if (flatFive)
        return fail ("b5 は m7(b5) の形だけ対応しています");

    if (add9)
    {
        if (q == "maj")      q = "add9";
        else if (q == "m")   addTension (tensions, "9");
        else                 return fail ("add9 は三和音にだけ付けられます");
    }

    chord.quality = q;
    sortTensions (tensions);
    chord.tensions = tensions;

    ParseResult r;
    r.chord = chord;
    return r;
}

//==============================================================================
std::string format (const Chord& c)
{
    static const std::map<std::string, std::string> names = {
        { "maj", "" }, { "m", "m" }, { "dim", "dim" }, { "aug", "aug" }, { "sus2", "sus2" }, { "sus4", "sus4" },
        { "6", "6" }, { "m6", "m6" }, { "7", "7" }, { "maj7", "M7" }, { "m7", "m7" }, { "mM7", "mM7" },
        { "m7b5", "m7" }, { "dim7", "dim7" }, { "7sus4", "7sus4" }, { "add9", "add9" }
    };

    std::string s = c.root;
    auto it = names.find (c.quality);
    s += it != names.end() ? it->second : c.quality;

    std::vector<std::string> paren;

    if (c.quality == "m7b5")
        paren.push_back ("b5");

    for (auto& t : c.tensions)
        paren.push_back (t);

    if (! paren.empty())
    {
        s += "(";
        for (size_t i = 0; i < paren.size(); ++i)
            s += (i > 0 ? "," : "") + paren[i];
        s += ")";
    }

    if (c.bass)
        s += "/" + *c.bass;

    return s;
}

std::vector<int> chordIntervals (const Chord& c)
{
    static const std::map<std::string, std::vector<int>> table = {
        { "maj", { 0, 4, 7 } },      { "m", { 0, 3, 7 } },        { "dim", { 0, 3, 6 } },     { "aug", { 0, 4, 8 } },
        { "sus2", { 0, 2, 7 } },     { "sus4", { 0, 5, 7 } },     { "6", { 0, 4, 7, 9 } },    { "m6", { 0, 3, 7, 9 } },
        { "7", { 0, 4, 7, 10 } },    { "maj7", { 0, 4, 7, 11 } }, { "m7", { 0, 3, 7, 10 } },  { "mM7", { 0, 3, 7, 11 } },
        { "m7b5", { 0, 3, 6, 10 } }, { "dim7", { 0, 3, 6, 9 } },  { "7sus4", { 0, 5, 7, 10 } },
        { "add9", { 0, 4, 7, 14 } }
    };

    static const std::map<std::string, int> tensionIntervals = {
        { "b9", 13 }, { "9", 14 }, { "#9", 15 }, { "11", 17 }, { "#11", 18 }, { "b13", 20 }, { "13", 21 }
    };

    auto it = table.find (c.quality);
    std::vector<int> result = it != table.end() ? it->second : std::vector<int> { 0, 4, 7 };

    for (auto& t : c.tensions)
        if (auto ti = tensionIntervals.find (t); ti != tensionIntervals.end())
            if (std::find (result.begin(), result.end(), ti->second) == result.end())
                result.push_back (ti->second);

    return result;
}

//==============================================================================
namespace
{
    /** 上声部に入れる音（ピッチクラス）を優先度順に並べる（§3.8 ボイシング規則 3）。 */
    std::vector<int> upperCandidates (const Chord& c)
    {
        const int root = pitchClass (c.root);
        const auto intervals = chordIntervals (c);
        const bool hasFlatNine = std::find (c.tensions.begin(), c.tensions.end(), "b9") != c.tensions.end();

        auto has = [&] (int i) { return std::find (intervals.begin(), intervals.end(), i) != intervals.end(); };

        std::vector<int> ordered;   // ルートからの音程
        auto push = [&] (int i) { if (has (i)) ordered.push_back (i); };

        // 3度（sus は 4度 / 2度）
        push (3); push (4);
        if (! has (3) && ! has (4)) { push (5); push (2); }

        // 7度（6度）。dim7 の減7度は 9
        push (10); push (11);
        if (! has (10) && ! has (11)) push (9);

        // 変化した5度はコードの性格を決めるので優先する
        push (6); push (8);

        // テンション
        for (int t : { 13, 14, 15, 17, 18, 20, 21 })
            push (t);

        // ルート → 5度の順に残す（多すぎるときは 5度 → ルートの順に省く）
        if (! hasFlatNine)   // b9 はルートと短2度でぶつかるので、ルートはベースに任せる
            ordered.push_back (0);

        push (7);

        std::vector<int> pcs;

        for (int i : ordered)
        {
            const int pc = (root + i) % 12;

            if (std::find (pcs.begin(), pcs.end(), pc) == pcs.end())
                pcs.push_back (pc);
        }

        return pcs;
    }

    double movementCost (const std::vector<int>& from, const std::vector<int>& to)
    {
        if (from.size() == to.size())
        {
            double sum = 0;
            for (size_t i = 0; i < from.size(); ++i)
                sum += std::abs (from[i] - to[i]);
            return sum;
        }

        // 声部数が違う場合は、互いに最も近い音への距離の合計（対称）
        auto nearestSum = [] (const std::vector<int>& a, const std::vector<int>& b)
        {
            double sum = 0;
            for (int x : a)
            {
                int best = std::numeric_limits<int>::max();
                for (int y : b)
                    best = std::min (best, std::abs (x - y));
                sum += best;
            }
            return sum;
        };

        return (nearestSum (from, to) + nearestSum (to, from)) * 0.5 * (double) std::max (from.size(), to.size())
                 / (double) std::min (from.size(), to.size());
    }
}

Voicing voice (const Chord& c, const Voicing* previous, const VoicingRules& rules)
{
    Voicing v;

    // 1. ベース
    const int bassPc = c.bass ? pitchClass (*c.bass) : pitchClass (c.root);

    for (int n = rules.bassLow; n <= rules.bassHigh; ++n)
        if (n % 12 == (bassPc < 0 ? 0 : bassPc))
        {
            v.bass = n;
            break;
        }

    // 2〜3. 上声部の音
    auto pcs = upperCandidates (c);

    if (pcs.size() > (size_t) rules.maxUpperVoices)
        pcs.resize ((size_t) rules.maxUpperVoices);

    // 4. すべての配置を列挙
    std::vector<std::vector<int>> options (pcs.size());

    for (size_t i = 0; i < pcs.size(); ++i)
        for (int n = rules.upperLow; n <= rules.upperHigh; ++n)
            if (n % 12 == pcs[i])
                options[i].push_back (n);

    std::vector<int> best;
    double bestCost = std::numeric_limits<double>::max();

    auto evaluate = [&] (std::vector<int> voices, bool strict)
    {
        std::sort (voices.begin(), voices.end());

        if (std::adjacent_find (voices.begin(), voices.end()) != voices.end())
            return;

        const int span = voices.back() - voices.front();

        if (strict && span > rules.maxSpan)
            return;

        // 5. 濁り回避: 60 未満で隣り合う声部が短3度未満 / ベースと近すぎる
        for (size_t i = 0; i + 1 < voices.size(); ++i)
            if (voices[i] < 60 && voices[i + 1] - voices[i] < 3 && strict)
                return;

        if (voices.front() - v.bass < 4)
            return;

        double cost = 0;

        if (previous != nullptr && ! previous->upper.empty())
            cost = movementCost (previous->upper, voices);
        else
        {
            const double mean = std::accumulate (voices.begin(), voices.end(), 0.0) / (double) voices.size();
            cost = std::abs (mean - rules.firstChordCentre);
        }

        cost += span * 0.01;   // 同じなら狭い方

        if (cost < bestCost - 1e-9 || (std::abs (cost - bestCost) < 1e-9 && voices < best))
        {
            bestCost = cost;
            best = voices;
        }
    };

    for (bool strict : { true, false })
    {
        std::vector<size_t> idx (pcs.size(), 0);

        while (true)
        {
            std::vector<int> voices;
            for (size_t i = 0; i < pcs.size(); ++i)
                voices.push_back (options[i][idx[i]]);

            evaluate (voices, strict);

            size_t k = 0;
            for (; k < idx.size(); ++k)
            {
                if (++idx[k] < options[k].size())
                    break;
                idx[k] = 0;
            }

            if (k == idx.size())
                break;
        }

        if (! best.empty())
            break;
    }

    v.upper = best;
    return v;
}

std::vector<std::optional<Voicing>> voiceProgression (const std::vector<std::optional<Chord>>& chords, const VoicingRules& rules)
{
    std::vector<std::optional<Voicing>> result;
    std::optional<Voicing> previous;

    for (auto& c : chords)
    {
        if (! c)
        {
            result.push_back (std::nullopt);
            continue;
        }

        auto v = voice (*c, previous ? &*previous : nullptr, rules);
        result.push_back (v);
        previous = v;
    }

    return result;
}

} // namespace collab::chord
