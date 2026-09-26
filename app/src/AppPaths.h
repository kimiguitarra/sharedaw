#pragma once

#include "Common.h"

namespace AppPaths
{
    /** 同梱アセット（内蔵音源など）のフォルダ。見つからなければ存在しない File。 */
    juce::File getAssetsDir();

    /** アプリの設定・自動保存・セッション情報を置くフォルダ（ユーザーごと）。 */
    juce::File getAppDataDir();
}
