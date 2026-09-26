#include "collab/Time.h"

#include <chrono>
#include <ctime>

namespace collab
{

std::string nowUtcIso8601()
{
    const auto t = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now());
    std::tm tm {};

   #if defined (_WIN32)
    gmtime_s (&tm, &t);
   #else
    gmtime_r (&t, &tm);
   #endif

    char buffer[32];
    std::strftime (buffer, sizeof (buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

} // namespace collab
