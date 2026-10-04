# ShareDAW デスクトップアプリ（JUCE + Tracktion Engine + sfizz）

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

# --- LAME（MP3 の書き出し）--------------------------------------------------
include(${PROJECT_SOURCE_DIR}/cmake/Lame.cmake)

# --- JUCE / Tracktion Engine ------------------------------------------------
# JUCE は構成のときに juceaide を Debug（/Zi = 共有 PDB）でビルドする。sccache は /Zi を扱えず失敗するので、
# MSVC では juceaide にだけコンパイルキャッシュを渡さない（このアプリ本体のターゲットには後で戻す）
if(MSVC)
    set(SHAREDAW_SAVED_C_LAUNCHER "${CMAKE_C_COMPILER_LAUNCHER}")
    set(SHAREDAW_SAVED_CXX_LAUNCHER "${CMAKE_CXX_COMPILER_LAUNCHER}")
    set(SHAREDAW_SAVED_ENV_C_LAUNCHER "$ENV{CMAKE_C_COMPILER_LAUNCHER}")
    set(SHAREDAW_SAVED_ENV_CXX_LAUNCHER "$ENV{CMAKE_CXX_COMPILER_LAUNCHER}")
    set(CMAKE_C_COMPILER_LAUNCHER "")
    set(CMAKE_CXX_COMPILER_LAUNCHER "")
    unset(ENV{CMAKE_C_COMPILER_LAUNCHER})   # juceaide の CMake は環境変数からも読む
    unset(ENV{CMAKE_CXX_COMPILER_LAUNCHER})
endif()

add_subdirectory(external/tracktion_engine/modules/juce)

if(MSVC)
    set(CMAKE_C_COMPILER_LAUNCHER "${SHAREDAW_SAVED_C_LAUNCHER}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SHAREDAW_SAVED_CXX_LAUNCHER}")
    set(ENV{CMAKE_C_COMPILER_LAUNCHER} "${SHAREDAW_SAVED_ENV_C_LAUNCHER}")
    set(ENV{CMAKE_CXX_COMPILER_LAUNCHER} "${SHAREDAW_SAVED_ENV_CXX_LAUNCHER}")
endif()
add_subdirectory(external/tracktion_engine/modules)

# ASIO（Windows）: Steinberg の ASIO SDK のパスを渡したときだけ有効にする（§3.5, M2）
set(COLLAB_ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (contains common/iasiodrv.h)")

set(SHAREDAW_BUILD_NUMBER 0 CACHE STRING "Build number used by the in-app updater")

# CI のビルドはバージョンの末尾をビルド番号にする（V0.1.123。アプリの「ShareDAW について」・更新の表示と同じ）
if(SHAREDAW_BUILD_NUMBER GREATER 0)
    set(SHAREDAW_APP_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${SHAREDAW_BUILD_NUMBER}")
else()
    set(SHAREDAW_APP_VERSION "${PROJECT_VERSION}")
endif()

juce_add_gui_app(ShareDAW
    PRODUCT_NAME "ShareDAW"
    COMPANY_NAME "ShareDAW"
    BUNDLE_ID "local.sharedaw.app"
    VERSION ${SHAREDAW_APP_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "録音のためにマイク（オーディオ入力）を使用します。"
    HARDENED_RUNTIME_ENABLED TRUE
    # 他社のプラグイン（署名のチームが違う）を読み込めるように library validation を外す
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input com.apple.security.cs.disable-library-validation
    PLUGINHOST_AU TRUE)

target_sources(ShareDAW PRIVATE
    src/Main.cpp
    src/AppPaths.cpp
    src/audio/AudioFiles.cpp
    src/audio/Export.cpp
    src/audio/Mp3Export.cpp
    src/update/Updater.cpp
    src/audio/ChannelStripPlugin.cpp
    src/audio/BuiltinEffectPlugin.cpp
    src/audio/MasterLimiterPlugin.cpp
    src/audio/MidiImport.cpp
    src/audio/CountInPlugin.cpp
    src/audio/Takes.cpp
    src/EngineBridge.cpp
    src/EngineBridgeRecording.cpp
    src/EngineBridgeRender.cpp
    src/InstrumentLibrary.cpp
    src/ProjectDocument.cpp
    src/SessionGuard.cpp
    src/SfizzPlugin.cpp
    src/Translations.cpp
    src/plugins/PluginHost.cpp
    src/plugins/HostSyncedPlugin.cpp
    src/sync/CredentialStore.cpp
    src/sync/SyncClient.cpp
    src/sync/SyncManager.cpp
    src/ui/AppContext.cpp
    src/ui/ChannelStripEditor.cpp
    src/ui/CompressorPanel.cpp
    src/ui/EqGraph.cpp
    src/ui/ChordEditor.cpp
    src/ui/ChordLane.cpp
    src/ui/Ruler.cpp
    src/ui/Theme.cpp
    src/ui/MainComponent.cpp
    src/ui/MainComponentCommands.cpp
    src/ui/MainComponentFiles.cpp
    src/ui/MainComponentRecording.cpp
    src/ui/MainComponentWindows.cpp
    src/ui/MainComponentSync.cpp
    src/ui/MainComponentUpdate.cpp
    src/ui/KeyLane.cpp
    src/ui/MarkerLane.cpp
    src/ui/MidiInputPanel.cpp
    src/ui/MasterPanel.cpp
    src/ui/MixerView.cpp
    src/ui/Inspector.cpp
    src/ui/BuiltinEffectEditor.cpp
    src/ui/SyncUI.cpp
    src/ui/SyncPanel.cpp
    src/ui/TransportBar.cpp
    src/ui/TimelineView.cpp
    src/ui/TimelineRangeClipboard.cpp
    src/ui/TrackLanes.cpp
    src/ui/TrackHeader.cpp
    src/ui/TempoMeterLanes.cpp
    src/ui/PianoRoll.cpp
    src/ui/PianoNoteGrid.cpp
    src/ui/StaffView.cpp
    src/ui/PianoRollEditing.cpp
    src/ui/PianoRollParts.cpp
    src/ui/PianoRollPage.cpp
    src/ui/ProjectPicker.cpp
    src/ui/InstrumentPanel.cpp
    src/ui/Dialogs.cpp)

target_include_directories(ShareDAW PRIVATE src)

target_compile_definitions(ShareDAW PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ShareDAW,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ShareDAW,JUCE_VERSION>"
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_USE_MP3AUDIOFORMAT=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

# アプリの更新（§ アップデート）: CI が実行番号を渡す。手元のビルドは 0。再コンパイルを最小にするため Updater.cpp だけに渡す
set_source_files_properties(src/update/Updater.cpp PROPERTIES COMPILE_DEFINITIONS "SHAREDAW_BUILD_NUMBER=${SHAREDAW_BUILD_NUMBER}")

if(COLLAB_ASIO_SDK_DIR)
    target_compile_definitions(ShareDAW PRIVATE JUCE_ASIO=1)
    target_include_directories(ShareDAW PRIVATE "${COLLAB_ASIO_SDK_DIR}/common")
endif()

target_link_libraries(ShareDAW PRIVATE
    collab::core
    mp3lame
    sfizz::sfizz
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_warning_flags)

# JUCE の推奨フラグは MSVC で /MP を付けるが、Ninja + sccache（CI のコンパイルキャッシュ）では不要で、キャッシュの邪魔になる。
# MSVC は同じ最適化だけを自前で付ける
if(MSVC)
    target_compile_options(ShareDAW PRIVATE $<$<CONFIG:Release>:/Ox> /EHsc)
else()
    target_link_libraries(ShareDAW PRIVATE juce::juce_recommended_config_flags)
endif()

set_target_properties(ShareDAW PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(ShareDAW PRIVATE -Wno-unknown-pragmas)
endif()

if(APPLE)
    target_compile_options(ShareDAW PRIVATE -fno-aligned-allocation)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(ShareDAW PRIVATE atomic)
    # Linux は開発・CI 用。HTTP（同期）に libcurl を使う
    target_compile_definitions(ShareDAW PRIVATE JUCE_USE_CURL=1)
    target_link_libraries(ShareDAW PRIVATE curl)
endif()

if(APPLE)
    target_link_libraries(ShareDAW PRIVATE "-framework Security")   # キーチェーン（§6.2）
endif()

# --- 内蔵音源などのアセットを実行ファイルの隣（Mac はバンドルの Resources）へコピー ---
if(APPLE)
    set(COLLAB_ASSETS_DEST "$<TARGET_BUNDLE_CONTENT_DIR:ShareDAW>/Resources/assets")
else()
    set(COLLAB_ASSETS_DEST "$<TARGET_FILE_DIR:ShareDAW>/assets")
endif()

add_custom_command(TARGET ShareDAW POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets" "${COLLAB_ASSETS_DEST}"
    COMMENT "Copying assets")

# 動作確認用のテストプラグイン（既定ではビルドしない）
option(COLLAB_BUILD_TEST_PLUGINS "Build test VST3 plugins for manual/automated testing" OFF)

if(COLLAB_BUILD_TEST_PLUGINS)
    add_subdirectory(${PROJECT_SOURCE_DIR}/tools/test-plugins ${CMAKE_BINARY_DIR}/test-plugins)
endif()
