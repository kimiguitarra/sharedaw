#include "Translations.h"

#include "Common.h"

void installJapaneseTranslations()
{
    const std::pair<const char*, juce::String> mappings[] = {
        // オーディオ設定（AudioDeviceSelectorComponent）
        { "Audio device type:",             "ドライバ:"_ju },
        { "Device:",                        "デバイス:"_ju },
        { "Output:",                        "出力:"_ju },
        { "Input:",                         "入力:"_ju },
        { "Active output channels:",        "使う出力チャンネル:"_ju },
        { "Active input channels:",         "使う入力チャンネル:"_ju },
        { "Sample rate:",                   "サンプルレート:"_ju },
        { "Audio buffer size:",             "バッファサイズ:"_ju },
        { "Active MIDI inputs:",            "使う MIDI 入力:"_ju },
        { "MIDI Output:",                   "MIDI 出力:"_ju },
        { "No MIDI inputs available",       "MIDI 入力がありません"_ju },
        { "Test",                           "テスト"_ju },
        { "Plays a test tone",              "テスト音を鳴らす"_ju },
        { "Control Panel",                  "コントロールパネル"_ju },
        { "Opens the device's own control panel", "デバイスのコントロールパネルを開く"_ju },
        { "Reset Device",                   "デバイスをリセット"_ju },
        { "Show advanced settings...",      "詳細設定を表示…"_ju },
        { "(no audio input channels found)",  "（入力チャンネルがありません）"_ju },
        { "(no audio output channels found)", "（出力チャンネルがありません）"_ju },
        { "Error when trying to open audio device!", "オーディオデバイスを開けませんでした"_ju },
        { "samples",                        "サンプル"_ju },
        { "none",                           "なし"_ju },
        { "OK",                             "OK" },
        { "Cancel",                         "キャンセル"_ju },

        // プラグイン一覧（PluginListComponent）
        { "Name",                           "名前"_ju },
        { "Format",                         "形式"_ju },
        { "Category",                       "種類"_ju },
        { "Manufacturer",                   "メーカー"_ju },
        { "Description",                    "説明"_ju },
        { "Options...",                     "オプション…"_ju },
        { "Clear list",                     "一覧を消去"_ju },
        { "Remove selected plug-in from list", "選択したプラグインを一覧から削除"_ju },
        { "Remove any plug-ins whose files no longer exist", "ファイルがなくなったプラグインを削除"_ju },
        { "Show folder containing selected plug-in", "プラグインのフォルダを表示"_ju },
        { "Scanning for plug-ins...",       "プラグインをスキャンしています…"_ju },
        { "Searching for all possible plug-in files...", "プラグインのファイルを探しています…"_ju },
        { "Scan complete",                  "スキャンが終わりました"_ju },
        { "Plugin Scanning",                "プラグインのスキャン"_ju },
        { "Deactivated after failing to initialise correctly", "正しく読み込めなかったため無効"_ju },
    };

    juce::String text ("language: Japanese\n");

    for (auto& [original, translated] : mappings)
        text << "\"" << original << "\" = \"" << translated << "\"\n";

    juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (text, false));
}
