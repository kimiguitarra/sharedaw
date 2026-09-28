#include "SyncClient.h"

namespace
{
    constexpr int timeoutMs = 30000;
}

juce::String ApiResponse::message() const
{
    if (status == 0)
        return "サーバーに接続できません"_ju + (networkError.isNotEmpty() ? "（"_ju + networkError + "）"_ju : juce::String());

    if (body.is_object() && body.contains ("message") && body["message"].is_string())
        return toJuce (body["message"].get<std::string>());

    return "サーバーエラー（HTTP "_ju + juce::String (status) + "）"_ju;
}

TransferUrl TransferUrl::fromJson (const nlohmann::json& j)
{
    TransferUrl t;
    t.hash = j.value ("hash", std::string());
    t.url = toJuce (j.value ("url", std::string()));
    t.method = toJuce (j.value ("method", std::string ("GET")));
    t.authRequired = j.value ("authRequired", false);

    if (auto it = j.find ("headers"); it != j.end() && it->is_object())
        for (auto& [k, v] : it->items())
            if (v.is_string())
                t.headers.set (toJuce (k), toJuce (v.get<std::string>()));

    return t;
}

SyncClient::SyncClient (juce::String url, juce::String t)
    : serverUrl (url.trim().trimCharactersAtEnd ("/")), token (t.trim())
{
}

ApiResponse SyncClient::request (const juce::String& method, const juce::String& path, const std::string* body) const
{
    ApiResponse r;
    juce::URL url (serverUrl + path);
    juce::String headers = "Authorization: Bearer " + token;

    if (body != nullptr)
    {
        url = url.withPOSTData (juce::MemoryBlock (body->data(), body->size()));
        headers << "\r\nContent-Type: application/json";
    }

    auto options = juce::URL::InputStreamOptions (body != nullptr ? juce::URL::ParameterHandling::inPostData
                                                                   : juce::URL::ParameterHandling::inAddress)
                     .withExtraHeaders (headers)
                     .withHttpRequestCmd (method)
                     .withConnectionTimeoutMs (timeoutMs)
                     .withStatusCode (&r.status);

    auto stream = url.createInputStream (options);

    if (stream == nullptr)
    {
        if (r.status == 0)
            r.networkError = "接続できませんでした"_ju;

        return r;
    }

    auto text = stream->readEntireStreamAsString().toStdString();

    try
    {
        r.body = text.empty() ? nlohmann::json() : nlohmann::json::parse (text);
    }
    catch (const std::exception&)
    {
        r.body = nlohmann::json { { "message", text.substr (0, 200) } };
    }

    return r;
}

ApiResponse SyncClient::get (const juce::String& path) const                         { return request ("GET", path, nullptr); }
ApiResponse SyncClient::del (const juce::String& path) const                         { return request ("DELETE", path, nullptr); }

ApiResponse SyncClient::patch (const juce::String& path, const nlohmann::json& body) const
{
    const auto text = body.dump();
    return request ("PATCH", path, &text);
}

ApiResponse SyncClient::post (const juce::String& path, const nlohmann::json& body) const
{
    const auto text = body.dump();
    return request ("POST", path, &text);
}

juce::Result SyncClient::uploadBlob (const TransferUrl& t, const juce::MemoryBlock& data,
                                    const TransferProgress& progress, std::atomic<bool>* directBroken) const
{
    bool cancelled = false;

    auto put = [&] (const juce::String& url, const juce::StringPairArray& extra, bool withAuth, juce::String& errorText) -> int
    {
        int status = 0;
        juce::String headers = "Content-Type: application/octet-stream";

        for (auto& key : extra.getAllKeys())
            headers << "\r\n" << key << ": " << extra[key];

        if (withAuth)
            headers << "\r\nAuthorization: Bearer " << token;

        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                               .withExtraHeaders (headers)
                               .withHttpRequestCmd ("PUT")
                               .withConnectionTimeoutMs (timeoutMs)
                               .withStatusCode (&status)
                               .withProgressCallback ([&] (int sent, int total)
                               {
                                   if (progress && ! progress (sent, total))
                                       cancelled = true;

                                   return ! cancelled;
                               });

        auto stream = juce::URL (url).withPOSTData (data).createInputStream (options);

        if (stream != nullptr)
        {
            const auto body = stream->readEntireStreamAsString();

            // S3 / R2 のエラーは XML（<Code>…</Code><Message>…</Message>）
            if (status < 200 || status >= 300)
            {
                const auto code = body.fromFirstOccurrenceOf ("<Code>", false, false).upToFirstOccurrenceOf ("</Code>", false, false);
                const auto message = body.fromFirstOccurrenceOf ("<Message>", false, false).upToFirstOccurrenceOf ("</Message>", false, false);
                errorText = code.isNotEmpty() ? code + (message.isNotEmpty() ? ": " + message : juce::String()) : body.substring (0, 200);
            }
        }

        return stream == nullptr ? 0 : status;
    };

    auto ok = [] (int status) { return status >= 200 && status < 300; };
    const auto viaServer = serverUrl + "/blobs/" + toJuce (t.hash) + "/data";

    // 前に署名付き URL で失敗していたら、最初からサーバー経由で送る
    if (! t.authRequired && directBroken != nullptr && directBroken->load())
    {
        juce::String error;
        const int status = put (viaServer, {}, true, error);

        if (cancelled)
            return juce::Result::fail ("中止しました"_ju);

        return ok (status) ? juce::Result::ok()
                           : juce::Result::fail ("アップロードに失敗しました（サーバー経由: HTTP "_ju + juce::String (status)
                                                 + (error.isNotEmpty() ? " " + error : juce::String()) + "）"_ju);
    }

    juce::String error;
    int status = put (t.url, t.headers, t.authRequired, error);

    if (cancelled)
        return juce::Result::fail ("中止しました"_ju);

    // 署名付き URL（R2 へ直接）で失敗したら、サーバー（Worker）経由で送り直す
    if (! ok (status) && ! t.authRequired)
    {
        if (directBroken != nullptr)
            *directBroken = true;

        DBG ("direct upload failed: HTTP " << status << " " << error);

        juce::String fallbackError;
        const int fallback = put (viaServer, {}, true, fallbackError);

        if (cancelled)
            return juce::Result::fail ("中止しました"_ju);

        if (ok (fallback))
            return juce::Result::ok();   // Worker 経由のアップロードはその場で検証・登録される

        return juce::Result::fail ("アップロードに失敗しました（直接: HTTP "_ju + juce::String (status)
                                   + (error.isNotEmpty() ? " " + error : juce::String())
                                   + "、サーバー経由: HTTP "_ju + juce::String (fallback)
                                   + (fallbackError.isNotEmpty() ? " " + fallbackError : juce::String()) + "）"_ju);
    }

    if (! ok (status))
        return juce::Result::fail ("アップロードに失敗しました（HTTP "_ju + juce::String (status)
                                   + (error.isNotEmpty() ? " " + error : juce::String()) + "）"_ju);

    // 署名付き URL で直接送った場合は、サーバーにハッシュを検証してもらう
    if (! t.authRequired)
    {
        auto r = post ("/blobs/" + toJuce (t.hash) + "/complete", nlohmann::json::object());

        if (! r.ok())
            return juce::Result::fail (r.message());
    }

    return juce::Result::ok();
}

juce::Result SyncClient::download (const TransferUrl& t, juce::MemoryBlock& out, const TransferProgress& progress) const
{
    int status = 0;
    juce::String headers;

    if (t.authRequired)
        headers << "Authorization: Bearer " << token;

    auto stream = juce::URL (t.url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                         .withExtraHeaders (headers)
                                                         .withConnectionTimeoutMs (timeoutMs)
                                                         .withStatusCode (&status));

    if (stream == nullptr || status < 200 || status >= 300)
        return juce::Result::fail ("ダウンロードに失敗しました（HTTP "_ju + juce::String (status) + "）"_ju);

    out.reset();
    const auto total = stream->getTotalLength();
    juce::HeapBlock<char> buffer (64 * 1024);

    for (;;)
    {
        const int n = stream->read (buffer.get(), 64 * 1024);

        if (n <= 0)
            break;

        out.append (buffer.get(), (size_t) n);

        if (progress && ! progress ((juce::int64) out.getSize(), total))
            return juce::Result::fail ("中止しました"_ju);
    }

    if (total > 0 && (juce::int64) out.getSize() != total)
        return juce::Result::fail ("ダウンロードが途中で切れました"_ju);

    return juce::Result::ok();
}
