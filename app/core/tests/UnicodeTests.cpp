#include <doctest/doctest.h>

#include "collab/Unicode.h"

using namespace collab;

TEST_CASE ("NFD Japanese is normalised to NFC")
{
    const std::string nfd = "\xe3\x81\x8b\xe3\x82\x99";   // か + 濁点（結合文字）
    const std::string nfc = "\xe3\x81\x8c";               // が
    CHECK (toNfc (nfd) == nfc);
    CHECK (toNfc (nfc) == nfc);
    CHECK (toNfc ("") == "");
}

TEST_CASE ("File names are sanitised for Windows")
{
    CHECK (sanitiseFileName ("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j");
    CHECK (sanitiseFileName ("name. ") == "name");
    CHECK (sanitiseFileName ("") == "_");
}
