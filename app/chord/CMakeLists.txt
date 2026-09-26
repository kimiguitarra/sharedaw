# collab_chord: コード表記のパーサーとボイシング（他の ShareDAW のコードに依存しない独立ライブラリ）
add_library(collab_chord STATIC src/Chord.cpp)
add_library(collab::chord ALIAS collab_chord)
target_include_directories(collab_chord PUBLIC include)
target_compile_features(collab_chord PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(collab_chord PRIVATE /W4)
else()
    target_compile_options(collab_chord PRIVATE -Wall -Wextra)
endif()

if(COLLAB_BUILD_TESTS)
    add_executable(collab_chord_tests tests/main.cpp tests/ParserTests.cpp tests/VoicingTests.cpp)
    target_link_libraries(collab_chord_tests PRIVATE collab::chord doctest::doctest)
    add_test(NAME collab_chord_tests COMMAND collab_chord_tests)
endif()
