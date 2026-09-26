# CollabDAW デスクトップアプリ（JUCE + Tracktion Engine + sfizz）

# --- sfizz（内蔵音源の SFZ プレイヤー）-------------------------------------
set(SFIZZ_JACK OFF CACHE BOOL "" FORCE)
set(SFIZZ_RENDER OFF CACHE BOOL "" FORCE)
set(SFIZZ_SHARED OFF CACHE BOOL "" FORCE)
set(SFIZZ_TESTS OFF CACHE BOOL "" FORCE)
set(SFIZZ_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEMOS OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEVTOOLS OFF CACHE BOOL "" FORCE)
set(SFIZZ_GIT_SUBMODULE_CHECK OFF CACHE BOOL "" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "" FORCE)
# sfizz は C++17 前提（C++20 では u8 リテラルの型が変わりビルドできない）
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(external/sfizz EXCLUDE_FROM_ALL)
set(CMAKE_CXX_STANDARD 20)

# --- JUCE / Tracktion Engine ------------------------------------------------
add_subdirectory(external/tracktion_engine/modules/juce)
add_subdirectory(external/tracktion_engine/modules)

# ASIO（Windows）: Steinberg の ASIO SDK のパスを渡したときだけ有効にする（§3.5, M2）
set(COLLAB_ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (contains common/iasiodrv.h)")

juce_add_gui_app(CollabDAW
    PRODUCT_NAME "CollabDAW"
    COMPANY_NAME "CollabDAW"
    BUNDLE_ID "local.collabdaw.app"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "録音のためにマイク（オーディオ入力）を使用します。"
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input
    PLUGINHOST_AU TRUE)

target_sources(CollabDAW PRIVATE
    src/Main.cpp
    src/AppPaths.cpp
    src/EngineBridge.cpp
    src/InstrumentLibrary.cpp
    src/ProjectDocument.cpp
    src/SessionGuard.cpp
    src/SfizzPlugin.cpp
    src/sync/CredentialStore.cpp
    src/sync/SyncClient.cpp
    src/sync/SyncManager.cpp
    src/ui/AppContext.cpp
    src/ui/ChordEditor.cpp
    src/ui/ChordLane.cpp
    src/ui/Ruler.cpp
    src/ui/Theme.cpp
    src/ui/MainComponent.cpp
    src/ui/MainComponentSync.cpp
    src/ui/SyncUI.cpp
    src/ui/TransportBar.cpp
    src/ui/TimelineView.cpp
    src/ui/TrackHeader.cpp
    src/ui/TempoMeterLanes.cpp
    src/ui/PianoRoll.cpp
    src/ui/InstrumentPanel.cpp
    src/ui/Dialogs.cpp)

target_include_directories(CollabDAW PRIVATE src)

target_compile_definitions(CollabDAW PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CollabDAW,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CollabDAW,JUCE_VERSION>"
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

if(COLLAB_ASIO_SDK_DIR)
    target_compile_definitions(CollabDAW PRIVATE JUCE_ASIO=1)
    target_include_directories(CollabDAW PRIVATE "${COLLAB_ASIO_SDK_DIR}/common")
endif()

target_link_libraries(CollabDAW PRIVATE
    collab::core
    sfizz::sfizz
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

set_target_properties(CollabDAW PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(CollabDAW PRIVATE -Wno-unknown-pragmas)
endif()

if(APPLE)
    target_compile_options(CollabDAW PRIVATE -fno-aligned-allocation)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(CollabDAW PRIVATE atomic)
    # Linux は開発・CI 用。HTTP（同期）に libcurl を使う
    target_compile_definitions(CollabDAW PRIVATE JUCE_USE_CURL=1)
    target_link_libraries(CollabDAW PRIVATE curl)
endif()

if(APPLE)
    target_link_libraries(CollabDAW PRIVATE "-framework Security")   # キーチェーン（§6.2）
endif()

# --- 内蔵音源などのアセットを実行ファイルの隣（Mac はバンドルの Resources）へコピー ---
if(APPLE)
    set(COLLAB_ASSETS_DEST "$<TARGET_BUNDLE_CONTENT_DIR:CollabDAW>/Resources/assets")
else()
    set(COLLAB_ASSETS_DEST "$<TARGET_FILE_DIR:CollabDAW>/assets")
endif()

add_custom_command(TARGET CollabDAW POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets" "${COLLAB_ASSETS_DEST}"
    COMMENT "Copying assets")
