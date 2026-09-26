#include <doctest/doctest.h>

#include "collab/Sha256.h"
#include "collab/Uuid.h"

using namespace collab;

TEST_CASE ("SHA-256 known vectors")
{
    CHECK (Sha256::hashHex ("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK (Sha256::hashHex ("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK (Sha256::hashHex ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
           == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    Sha256 h;
    const std::string a (1000000, 'a');
    for (size_t i = 0; i < a.size(); i += 997)
        h.update (a.data() + i, std::min<size_t> (997, a.size() - i));
    CHECK (h.finishHex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE ("UUID v4 format and uniqueness")
{
    auto a = generateUuid();
    auto b = generateUuid();
    CHECK (isValidUuid (a));
    CHECK (isValidUuid (b));
    CHECK (a != b);
    CHECK (a[14] == '4');
    CHECK ((a[19] == '8' || a[19] == '9' || a[19] == 'a' || a[19] == 'b'));
    CHECK_FALSE (isValidUuid ("6F1C1D7E-2A51-4E8E-9D3B-0D3A1D2F7A10"));
}
