#pragma once

#include "Common.h"

/**
    API トークンを OS の資格情報ストアに保存する（§6.2）。
    Windows: 資格情報マネージャー、macOS: キーチェーン。それ以外（開発用の Linux）はアプリのデータフォルダ。
*/
namespace CredentialStore
{
    bool saveToken (const juce::String& serverUrl, const juce::String& token);
    juce::String loadToken (const juce::String& serverUrl);
    void removeToken (const juce::String& serverUrl);
}
