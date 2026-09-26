#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace collab
{

/** ストリーミング可能な SHA-256。オーディオ実体のハッシュ名・フィンガープリントに使う。 */
class Sha256
{
public:
    Sha256();

    void update (const void* data, std::size_t numBytes);
    void update (std::string_view s)     { update (s.data(), s.size()); }

    /** 結果を 64 桁の小文字16進で返す。呼んだ後は再利用しない。 */
    std::string finishHex();
    std::array<std::uint8_t, 32> finish();

    static std::string hashHex (std::string_view data);

private:
    void processBlock (const std::uint8_t* block);

    std::array<std::uint32_t, 8> state {};
    std::array<std::uint8_t, 64> buffer {};
    std::size_t bufferSize = 0;
    std::uint64_t totalBytes = 0;
};

} // namespace collab
