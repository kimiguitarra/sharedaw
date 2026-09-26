#include <doctest/doctest.h>

#include <algorithm>
#include <set>

#include "collab/chord/Chord.h"

using namespace collab::chord;

namespace
{
    Chord p (const std::string& text)
    {
        auto r = parse (text);
        REQUIRE (r.chord.has_value());
        return *r.chord;
    }

    std::set<int> pcs (const std::vector<int>& notes)
    {
        std::set<int> s;
        for (int n : notes)
            s.insert (n % 12);
        return s;
    }

    void checkRules (const Voicing& v, const Chord& c, const VoicingRules& r = {})
    {
        CAPTURE (format (c));
        CHECK (v.bass >= r.bassLow);
        CHECK (v.bass <= r.bassHigh);
        CHECK (v.bass % 12 == pitchClass (c.bass ? *c.bass : c.root));

        REQUIRE (v.upper.size() >= 3);
        CHECK (v.upper.size() <= 4);
        CHECK (std::is_sorted (v.upper.begin(), v.upper.end()));

        for (int n : v.upper)
        {
            CHECK (n >= r.upperLow);
            CHECK (n <= r.upperHigh);
        }

        CHECK (v.upper.front() - v.bass >= 4);

        for (size_t i = 0; i + 1 < v.upper.size(); ++i)
            if (v.upper[i] < 60)
                CHECK (v.upper[i + 1] - v.upper[i] >= 3);

        // 上声部の音はすべてコードの構成音
        const int root = pitchClass (c.root);
        std::set<int> chordPcs;
        for (int i : chordIntervals (c))
            chordPcs.insert ((root + i) % 12);

        for (int n : v.upper)
            CHECK (chordPcs.count (n % 12) == 1);
    }
}

TEST_CASE ("first chord is centred around E4")
{
    auto v = voice (p ("C"));
    CHECK (v.bass == 36);
    CHECK (v.upper == std::vector<int> { 60, 64, 67 });
}

TEST_CASE ("thirds and sevenths are kept, fifth dropped first")
{
    auto c = p ("G7(9,13)");
    auto v = voice (c);
    checkRules (v, c);
    // G7(9,13): B, F, A, E（3度・7度・テンション）。ルートと5度は省く
    CHECK (pcs (v.upper) == std::set<int> { 11, 5, 9, 4 });

    auto c7 = p ("G7");
    auto v7 = voice (c7);
    // 3度・7度・ルート・5度の4音
    CHECK (pcs (v7.upper) == std::set<int> { 11, 5, 7, 2 });

    auto c9 = p ("C9");
    // 3度・7度・9th・ルート（5度を省く）
    CHECK (pcs (voice (c9).upper) == std::set<int> { 4, 10, 2, 0 });
}

TEST_CASE ("slash chord bass")
{
    auto c = p ("F/G");
    auto v = voice (c);
    CHECK (v.bass == 43);
    checkRules (v, c);

    auto am = p ("Am7/G");
    CHECK (voice (am).bass == 43);

    auto de = p ("C/E");
    CHECK (voice (de).bass == 40);
}

TEST_CASE ("b9 avoids the root in the upper voices")
{
    auto c = p ("G7(b9)");
    auto v = voice (c);
    checkRules (v, c);
    CHECK (pcs (v.upper).count (7) == 0);   // G を上声部に入れない
    CHECK (pcs (v.upper).count (8) == 1);   // Ab（b9）は入る
}

TEST_CASE ("voice leading minimises movement")
{
    // C → F: 60,64,67 → 60,65,69（C を保持、E→F、G→A）
    auto prog = voiceProgression ({ p ("C"), p ("F"), p ("G7"), p ("C") });
    REQUIRE (prog.size() == 4);
    CHECK (prog[0]->upper == std::vector<int> { 60, 64, 67 });
    CHECK (prog[1]->upper == std::vector<int> { 60, 65, 69 });

    // ii-V-I ではどの声部も大きく跳躍しない
    auto jazz = voiceProgression ({ p ("Dm7"), p ("G7"), p ("CM7") });

    for (size_t i = 1; i < jazz.size(); ++i)
    {
        auto& a = jazz[i - 1]->upper;
        auto& b = jazz[i]->upper;
        REQUIRE (a.size() == b.size());

        int total = 0;
        for (size_t k = 0; k < a.size(); ++k)
        {
            CHECK (std::abs (a[k] - b[k]) <= 5);
            total += std::abs (a[k] - b[k]);
        }
        CHECK (total <= 10);
    }
}

TEST_CASE ("no chord keeps the previous voicing as reference")
{
    auto prog = voiceProgression ({ p ("C"), std::nullopt, p ("F") });
    CHECK_FALSE (prog[1].has_value());
    CHECK (prog[2]->upper == std::vector<int> { 60, 65, 69 });
}

TEST_CASE ("every root and quality produces a valid voicing")
{
    for (auto root : { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" })
        for (auto& q : allQualities())
            for (auto tensions : { std::vector<std::string> {}, std::vector<std::string> { "9" }, std::vector<std::string> { "b9", "#11", "13" } })
            {
                Chord c { root, q, tensions, std::nullopt };
                checkRules (voice (c), c);

                Chord slash = c;
                slash.bass = "G";
                checkRules (voice (slash), slash);

                // 前のコードがあっても規則を守る
                Voicing prev { 40, { 65, 69, 72, 76 } };
                checkRules (voice (c, &prev), c);
            }
}
