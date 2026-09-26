# Header-only / small third-party libraries used by the core library.
include(FetchContent)

set(FETCHCONTENT_QUIET ON)

# utf8proc などは古い cmake_minimum_required を指定しており、CMake 4 以降では構成に失敗するため
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(json_schema_validator
    GIT_REPOSITORY https://github.com/pboettch/json-schema-validator.git
    GIT_TAG 2.3.0
    GIT_SHALLOW TRUE)
set(JSON_VALIDATOR_BUILD_TESTS OFF CACHE INTERNAL "")
set(JSON_VALIDATOR_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(JSON_VALIDATOR_INSTALL OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(json_schema_validator)

FetchContent_Declare(utf8proc
    GIT_REPOSITORY https://github.com/JuliaStrings/utf8proc.git
    GIT_TAG v2.9.0
    GIT_SHALLOW TRUE)
set(UTF8PROC_INSTALL OFF CACHE INTERNAL "")
set(UTF8PROC_ENABLE_TESTING OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(utf8proc)

if(COLLAB_BUILD_TESTS)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
        GIT_SHALLOW TRUE)
    set(DOCTEST_NO_INSTALL ON CACHE INTERNAL "")
    FetchContent_MakeAvailable(doctest)
endif()
