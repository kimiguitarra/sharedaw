#include "collab/Unicode.h"

#include <cstdlib>

#include <utf8proc.h>

namespace collab
{

std::string toNfc (const std::string& utf8)
{
    if (utf8.empty())
        return utf8;

    auto* out = utf8proc_NFC (reinterpret_cast<const utf8proc_uint8_t*> (utf8.c_str()));

    if (out == nullptr)
        return utf8;

    std::string result (reinterpret_cast<const char*> (out));
    std::free (out);
    return result;
}

std::string sanitiseFileName (const std::string& utf8)
{
    std::string s = toNfc (utf8);

    for (auto& c : s)
    {
        const auto u = static_cast<unsigned char> (c);

        if (u < 0x20 || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?'
             || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }

    // Windows では末尾のピリオド・空白が使えない
    while (! s.empty() && (s.back() == '.' || s.back() == ' '))
        s.pop_back();

    if (s.empty())
        s = "_";

    return s;
}

} // namespace collab
