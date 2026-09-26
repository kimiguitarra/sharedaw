# 動作確認用のテストプラグイン（VST3）。-DCOLLAB_BUILD_TEST_PLUGINS=ON のときだけビルドする。
#   CollabTestSynth : サイン波のシンセ（音量パラメータあり、状態を保存できる）
#   CollabCrashPlugin : 読み込むとクラッシュする（スキャン時のブラックリスト登録の確認用）

juce_add_plugin(CollabTestSynth
    PLUGIN_MANUFACTURER_CODE Cldw
    PLUGIN_CODE Ctsy
    COMPANY_NAME "ShareDAW Test"
    PRODUCT_NAME "CollabTestSynth"
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(CollabTestSynth PRIVATE TestSynth.cpp)
target_compile_definitions(CollabTestSynth PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 COLLAB_TEST_CRASH=0)
target_link_libraries(CollabTestSynth PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)

juce_add_plugin(CollabCrashPlugin
    PLUGIN_MANUFACTURER_CODE Cldw
    PLUGIN_CODE Ccrs
    COMPANY_NAME "ShareDAW Test"
    PRODUCT_NAME "CollabCrashPlugin"
    FORMATS VST3
    IS_SYNTH FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(CollabCrashPlugin PRIVATE TestSynth.cpp)
target_compile_definitions(CollabCrashPlugin PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 COLLAB_TEST_CRASH=1)
target_link_libraries(CollabCrashPlugin PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
