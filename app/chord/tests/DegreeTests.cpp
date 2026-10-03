#include <doctest/doctest.h>

#include "collab/chord/Degree.h"

using namespace collab::chord;

namespace
{
    std::string deg (const std::string& chordText, Key k)
    {
        auto r = parse (chordText);
        REQUIRE (r.chord);
        return degreeName (*r.chord, k);
    }

    std::string in (const std::string& input, Key k)
    {
        return degreeToChordText (input, k).value_or ("(none)");
    }

    const Key C { 0, false }, G { 7, false }, F { 5, false }, Eb { 3, false }, Am { 9, true }, Em { 4, true };
}

TEST_CASE ("key names follow the key signature")
{
    CHECK (keyName (C) == "C");
    CHECK (keyName (Eb) == "Eb");
    CHECK (keyName (Am) == "Am");
    CHECK (keyName ({ 6, false }) == "Gb");
    CHECK (keyName ({ 1, true }) == "C#m");
    CHECK (spellPitch (10, F) == "Bb");
    CHECK (spellPitch (6, G) == "F#");
}

TEST_CASE ("chords are shown as degrees in the key")
{
    CHECK (deg ("C", C) == "Ⅰ");
    CHECK (deg ("Am7", C) == "Ⅵm7");
    CHECK (deg ("G7(9)", C) == "Ⅴ7(9)");
    CHECK (deg ("FM7", C) == "ⅣM7");
    CHECK (deg ("Bb", C) == "♭Ⅶ");
    CHECK (deg ("F#m7-5", C) == "♯Ⅳm7(b5)");
    CHECK (deg ("C/E", C) == "Ⅰ/Ⅲ");
    CHECK (deg ("Em7", G) == "Ⅵm7");
    CHECK (deg ("C", Am) == "Ⅲ");
    CHECK (deg ("E7", Am) == "Ⅴ7");
    CHECK (deg ("G#dim7", Am) == "♯Ⅶdim7");
    CHECK (deg ("Bb", Am) == "♭Ⅱ");
}

TEST_CASE ("degree input becomes chords in the key")
{
    CHECK (in ("1", C) == "CM7");
    CHECK (in ("2", C) == "Dm7");
    CHECK (in ("7", C) == "Bm7(b5)");
    CHECK (in ("5", C) == "G7");
    CHECK (in ("6m", C) == "Am");
    CHECK (in ("1maj", C) == "Cmaj");
    CHECK (in ("6m7", C) == "Am7");
    CHECK (in ("57", C) == "G7");
    CHECK (in ("4M7", C) == "FM7");
    CHECK (in ("b7", C) == "Bb");
    CHECK (in ("#4m7-5", C) == "F#m7-5");
    CHECK (in ("1/3", C) == "C/E");
    CHECK (in ("4/5", C) == "F/G");
    CHECK (in ("５", G) == "D7");
    CHECK (in ("2", F) == "Gm7");
    CHECK (in ("4", Eb) == "AbM7");
    CHECK (in ("1", Am) == "Am7");
    CHECK (in ("3", Am) == "CM7");
    CHECK (in ("57", Am) == "E7");
    CHECK (in ("5", Em) == "Bm7");
    CHECK (in ("IV", C) == "FM7");
    CHECK (in ("vi7", C) == "Am7");
    CHECK (in ("Ⅵm", C) == "Am");
    CHECK (in ("♭Ⅵ", C) == "Ab");

    // ディグリーでないものはそのまま（通常のコード表記として扱う）
    CHECK_FALSE (degreeToChordText ("Am7", C));
    CHECK_FALSE (degreeToChordText ("G7(9,13)", C));
    CHECK_FALSE (degreeToChordText ("X", C));
    CHECK_FALSE (degreeToChordText ("", C));
}

TEST_CASE ("keys are estimated from chord progressions")
{
    auto est = [] (std::vector<std::string> names)
    {
        std::vector<Chord> chords;
        for (auto& n : names)
            chords.push_back (*parse (n).chord);
        return estimateKey (chords);
    };

    CHECK (est ({ "C", "F", "G", "C" }) == Key { 0, false });
    CHECK (est ({ "FM7", "G7", "Em7", "Am7", "Dm7", "G7", "C" }) == Key { 0, false });   // 王道進行から Ⅰ へ
    CHECK (est ({ "Am", "Dm", "E7", "Am" }) == Key { 9, true });
    CHECK (est ({ "G", "D", "Em", "C" }) == Key { 7, false });
    CHECK (est ({ "Eb", "Bb", "Cm", "Ab" }) == Key { 3, false });
    CHECK_FALSE (est ({}));
}

TEST_CASE ("staff spelling follows the key signature")
{
    const Key c { 0, false }, f { 5, false }, d { 2, false }, gb { 6, false };

    // C 長調: 中央の C は 28、黒鍵は C# と Eb（spellPitch と同じ）
    CHECK (spellForStaff (60, c).step() == 28);
    CHECK (spellForStaff (60, c).accidental == 0);
    CHECK (spellForStaff (61, c).letter == 0);
    CHECK (spellForStaff (61, c).accidental == 1);
    CHECK (spellForStaff (63, c).letter == 2);
    CHECK (spellForStaff (63, c).accidental == -1);

    // F 長調は Bb、D 長調は F#・C#
    CHECK (spellForStaff (70, f).letter == 6);
    CHECK (spellForStaff (70, f).accidental == -1);
    CHECK (spellForStaff (66, d).letter == 3);
    CHECK (spellForStaff (66, d).accidental == 1);

    // Gb 長調の Cb は B の高さではなく C の位置（オクターブは幹音で数える）
    const auto cb = spellForStaff (59, gb);
    CHECK (cb.letter == 0);
    CHECK (cb.accidental == -1);
    CHECK (cb.octave == 4);

    CHECK (keySignatureAccidental (6, f) == -1);
    CHECK (keySignatureAccidental (3, d) == 1);
    CHECK (keySignatureAccidental (3, c) == 0);
}
