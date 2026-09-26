#include <doctest/doctest.h>

#include "collab/chord/Chord.h"

using namespace collab::chord;

namespace
{
    Chord p (const std::string& text)
    {
        auto r = parse (text);
        INFO (text << " : " << r.error);
        REQUIRE (r.chord.has_value());
        return *r.chord;
    }

    void expect (const std::string& text, const std::string& root, const std::string& quality,
                 std::vector<std::string> tensions = {}, std::optional<std::string> bass = {})
    {
        CAPTURE (text);
        auto c = p (text);
        CHECK (c.root == root);
        CHECK (c.quality == quality);
        CHECK (c.tensions == tensions);
        CHECK (c.bass == bass);
    }
}

TEST_CASE ("triads")
{
    expect ("C", "C", "maj");
    expect ("Cm", "C", "m");
    expect ("C#m", "C#", "m");
    expect ("Bbdim", "Bb", "dim");
    expect ("Caug", "C", "aug");
    expect ("C+", "C", "aug");
    expect ("Dsus2", "D", "sus2");
    expect ("Dsus4", "D", "sus4");
    expect ("Dsus", "D", "sus4");
    expect ("Cmin", "C", "m");
    expect ("C-", "C", "m");
}

TEST_CASE ("four-note chords and spelling variants")
{
    expect ("C6", "C", "6");
    expect ("Am6", "A", "m6");
    expect ("G7", "G", "7");
    expect ("CM7", "C", "maj7");
    expect ("Cmaj7", "C", "maj7");
    expect ("C\xE2\x96\xB3" "7", "C", "maj7");    // C△7
    expect ("C\xCE\x94" "7", "C", "maj7");        // CΔ7
    expect ("Dm7", "D", "m7");
    expect ("CmM7", "C", "mM7");
    expect ("Cm(maj7)", "C", "mM7");
    expect ("Bm7-5", "B", "m7b5");
    expect ("Bm7(b5)", "B", "m7b5");
    expect ("B\xC3\xB8", "B", "m7b5");            // Bø
    expect ("B\xC3\xB8" "7", "B", "m7b5");
    expect ("Cdim7", "C", "dim7");
    expect ("G7sus4", "G", "7sus4");
    expect ("Cadd9", "C", "add9");
}

TEST_CASE ("tensions")
{
    expect ("G7(9,13)", "G", "7", { "9", "13" });
    expect ("G7(13,9)", "G", "7", { "9", "13" });
    expect ("C9", "C", "7", { "9" });
    expect ("CM9", "C", "maj7", { "9" });
    expect ("Am9", "A", "m7", { "9" });
    expect ("Am11", "A", "m7", { "9", "11" });
    expect ("G13", "G", "7", { "9", "13" });
    expect ("G7(b9)", "G", "7", { "b9" });
    expect ("G7b9", "G", "7", { "b9" });
    expect ("G7(#9,b13)", "G", "7", { "#9", "b13" });
    expect ("G7(-9)", "G", "7", { "b9" });
    expect ("C7(#11)", "C", "7", { "#11" });
    expect ("CM7(9)", "C", "maj7", { "9" });
    expect ("C6(9)", "C", "6", { "9" });
    expect ("C69", "C", "6", { "9" });
    expect ("C6/9", "C", "6", { "9" });
    expect ("Cm7(9, 11)", "C", "m7", { "9", "11" });
    expect ("Bm7(b5,11)", "B", "m7b5", { "11" });
    expect ("Cmadd9", "C", "m", { "9" });
}

TEST_CASE ("slash chords")
{
    expect ("C/E", "C", "maj", {}, "E");
    expect ("F/G", "F", "maj", {}, "G");
    expect ("Am7/G", "A", "m7", {}, "G");
    expect ("D/F#", "D", "maj", {}, "F#");
    expect ("C/Bb", "C", "maj", {}, "Bb");
    expect ("G7(9)/F", "G", "7", { "9" }, "F");
}

TEST_CASE ("unicode and full-width input")
{
    expect ("F\xE2\x99\xAF" "m7", "F#", "m7");                    // F♯m7
    expect ("B\xE2\x99\xAD", "Bb", "maj");                         // B♭
    expect ("G7\xEF\xBC\x88" "9\xEF\xBC\x8C" "13\xEF\xBC\x89", "G", "7", { "9", "13" });   // G7（9，13）
    expect (" C M7 ", "C", "maj7");
}

TEST_CASE ("no chord")
{
    CHECK (parse ("X").noChord);
    CHECK (parse ("N.C.").noChord);
}

TEST_CASE ("errors")
{
    for (auto bad : { "", "H", "Cxyz", "C7(10)", "C/", "C/H", "C7(9", "Caug7", "C7sus2", "Cmsus4", "C7b5" })
    {
        CAPTURE (bad);
        auto r = parse (bad);
        CHECK_FALSE (r.ok());
        CHECK_FALSE (r.error.empty());
    }
}

TEST_CASE ("format and round trip")
{
    CHECK (format (p ("Cmaj7(9)")) == "CM7(9)");
    CHECK (format (p ("Bm7-5")) == "Bm7(b5)");
    CHECK (format (p ("Bm7(b5,11)")) == "Bm7(b5,11)");
    CHECK (format (p ("F/G")) == "F/G");
    CHECK (format (p ("G13")) == "G7(9,13)");
    CHECK (format (p ("C")) == "C");

    for (auto& root : { "C", "C#", "Db", "F#", "Bb", "B" })
        for (auto& q : allQualities())
        {
            Chord c { root, q, {}, std::nullopt };

            if (q == "7" || q == "m7" || q == "maj7")
                c.tensions = { "9", "13" };

            c.bass = std::string ("E");
            CAPTURE (format (c));
            auto r = parse (format (c));
            REQUIRE (r.chord);
            CHECK (*r.chord == c);
        }
}

TEST_CASE ("pitch classes and intervals")
{
    CHECK (pitchClass ("C") == 0);
    CHECK (pitchClass ("C#") == 1);
    CHECK (pitchClass ("Db") == 1);
    CHECK (pitchClass ("B") == 11);
    CHECK (pitchClass ("Cb") == 11);
    CHECK (pitchClass ("H") == -1);

    CHECK (chordIntervals (p ("G7(9,13)")) == std::vector<int> { 0, 4, 7, 10, 14, 21 });
    CHECK (chordIntervals (p ("Cdim7")) == std::vector<int> { 0, 3, 6, 9 });
}
