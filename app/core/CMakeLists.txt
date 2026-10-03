# collab_core: JUCE / Tracktion に依存しないロジック（モデル、JSON、テンポマップ、内蔵音源の SFZ 生成など）

set(COLLAB_SCHEMA_FILE "${PROJECT_SOURCE_DIR}/shared/schema/project.schema.json")
set(COLLAB_SCHEMA_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/ProjectSchemaData.h")

add_custom_command(
    OUTPUT "${COLLAB_SCHEMA_HEADER}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${COLLAB_SCHEMA_FILE} -DOUTPUT=${COLLAB_SCHEMA_HEADER}
            -P "${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake"
    DEPENDS "${COLLAB_SCHEMA_FILE}" "${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake"
    COMMENT "Embedding project JSON schema")

add_library(collab_core STATIC
    src/BuiltinInstruments.cpp
    src/BuiltinEffects.cpp
    src/ChannelStripDsp.cpp
    src/MasterDsp.cpp
    src/ChordPlayback.cpp
    src/ClipEditing.cpp
    src/Stretch.cpp
    src/GmDrumMap.cpp
    src/MidiExport.cpp
    src/Grid.cpp
    src/Project.cpp
    src/ProjectDiff.cpp
    src/ProjectJson.cpp
    src/Recording.cpp
    src/Render.cpp
    src/Sha256.cpp
    src/TempoMap.cpp
    src/Time.cpp
    src/Unicode.cpp
    src/Uuid.cpp
    "${COLLAB_SCHEMA_HEADER}")

add_library(collab::core ALIAS collab_core)

target_include_directories(collab_core
    PUBLIC include
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(collab_core
    PUBLIC nlohmann_json::nlohmann_json collab::chord
    PRIVATE nlohmann_json_schema_validator utf8proc)

target_compile_features(collab_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(collab_core PRIVATE /W4)
else()
    target_compile_options(collab_core PRIVATE -Wall -Wextra)
endif()
