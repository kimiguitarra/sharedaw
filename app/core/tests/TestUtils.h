#pragma once

#include <fstream>
#include <sstream>
#include <string>

inline std::string readTextFile (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

inline std::string fixture (const std::string& name)
{
    return readTextFile (std::string (COLLAB_FIXTURES_DIR) + "/" + name);
}
