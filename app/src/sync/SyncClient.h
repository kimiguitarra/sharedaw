#pragma once

#include <nlohmann/json.hpp>

#include "Common.h"

/** 同期サーバーの応答。 */
struct ApiResponse
{
    int status = 0;
    nlohmann::json body;
    juce::String networkError;

    bool ok() const noexcept          { return status >= 200 && status < 300; }
    std::string errorCode() const     { return body.is_object() ? body.value ("error", std::string()) : std::string(); }

    /** 利用者向けのエラーメッセージ。 */
    juce::String message() const;
};

/** 実体の転送先（署名付き URL、または開発用に Worker 経由）。 */
struct TransferUrl
{
    std::string hash;
    juce::String url;
    juce::String method;
    bool authRequired = false;
    juce::StringPairArray headers;   // 送るときに付けるヘッダー（署名付き URL のチェックサムなど）

    static TransferUrl fromJson (const nlohmann::json&);
};

/** 転送の進み具合（バイト）。false を返すと中止する。 */
using TransferProgress = std::function<bool (juce::int64 done, juce::int64 total)>;

/** 長い処理の進み具合（表示する文と 0〜1。不明なら -1）。false を返すと中止する。 */
using SyncProgress = std::function<bool (const juce::String& status, double fraction)>;

/**
    同期サーバーの HTTP クライアント（§6.5）。呼び出しはすべて同期的（バックグラウンドスレッドから呼ぶ）。
*/
class SyncClient
{
public:
    SyncClient (juce::String serverUrl, juce::String token);

    ApiResponse get (const juce::String& path) const;
    ApiResponse post (const juce::String& path, const nlohmann::json& body) const;
    ApiResponse del (const juce::String& path) const;

    /**
        実体をアップロードする（署名付き URL なら完了を通知してサーバーに検証させる）。
        署名付き URL で失敗したらサーバー（Worker）経由で送り直し、directBroken を true にする
        （true なら最初からサーバー経由で送る。同じファイルを 2 回送らない）。
    */
    juce::Result uploadBlob (const TransferUrl&, const juce::MemoryBlock& data,
                             const TransferProgress& progress = {}, std::atomic<bool>* directBroken = nullptr) const;

    /** 実体をダウンロードする。 */
    juce::Result download (const TransferUrl&, juce::MemoryBlock& out, const TransferProgress& progress = {}) const;

    const juce::String& getServerUrl() const noexcept   { return serverUrl; }

private:
    juce::String serverUrl, token;

    ApiResponse request (const juce::String& method, const juce::String& path, const std::string* body) const;
};
