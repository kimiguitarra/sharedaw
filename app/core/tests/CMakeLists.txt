add_executable(collab_core_tests
    main.cpp
    BuiltinInstrumentsTests.cpp
    ChannelStripTests.cpp
    MasterDspTests.cpp
    ChordPlaybackTests.cpp
    ClipEditingTests.cpp
    GridTests.cpp
    ProjectDiffTests.cpp
    ProjectJsonTests.cpp
    RecordingTests.cpp
    RenderTests.cpp
    Sha256Tests.cpp
    TempoMapTests.cpp
    UnicodeTests.cpp)

target_link_libraries(collab_core_tests PRIVATE collab::core doctest::doctest)
target_compile_definitions(collab_core_tests PRIVATE
    COLLAB_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/shared/fixtures"
    COLLAB_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")

add_test(NAME collab_core_tests COMMAND collab_core_tests)
