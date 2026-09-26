#include "collab/Uuid.h"

#include <cstdint>
#include <mutex>
#include <random>

namespace collab
{

std::string generateUuid()
{
    static std::mutex mutex;
    static std::mt19937_64 rng = []
    {
        std::random_device rd;
        std::seed_seq seq { rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd() };
        return std::mt19937_64 (seq);
    }();

    std::uint64_t hi, lo;
    {
        std::lock_guard lock (mutex);
        hi = rng();
        lo = rng();
    }

    hi = (hi & 0xffffffffffff0fffull) | 0x0000000000004000ull;   // version 4
    lo = (lo & 0x3fffffffffffffffull) | 0x8000000000000000ull;   // variant 10

    static constexpr char hex[] = "0123456789abcdef";
    std::string s;
    s.reserve (36);

    auto put = [&] (std::uint64_t v, int nibbles)
    {
        for (int i = nibbles - 1; i >= 0; --i)
            s += hex[(v >> (i * 4)) & 0xf];
    };

    put (hi >> 32, 8);   s += '-';
    put (hi >> 16, 4);   s += '-';
    put (hi, 4);         s += '-';
    put (lo >> 48, 4);   s += '-';
    put (lo, 12);
    return s;
}

bool isValidUuid (const std::string& s)
{
    if (s.size() != 36)
        return false;

    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];

        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (c != '-')
                return false;
        }
        else if (! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }
    }

    return true;
}

} // namespace collab
