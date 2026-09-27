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

ApiResponse SyncClient::post (const juce::String& path, const nlohmann::json& body) const
{
    const auto text = body.dump();
    return request ("POST", path, &text);
}

juce::Result SyncClient::uploadBlob (const TransferUrl& t, const juce::MemoryBlock& data) const
{
    int status = 0;
    juce::String headers = "Content-Type: application/octet-stream";

    for (auto& key : t.headers.getAllKeys())
        headers << "\r\n" << key << ": " << t.headers[key];

    if (t.authRequired)
        headers << "\r\nAuthorization: Bearer " << token;

    auto stream = juce::URL (t.url).withPOSTData (data)
                    .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                          .withExtraHeaders (headers)
                                          .withHttpRequestCmd ("PUT")
                                          .withConnectionTimeoutMs (timeoutMs)
                                          .withStatusCode (&status));

    if (stream == nullptr || status < 200 || status >= 300)
        return juce::Result::fail ("アップロードに失敗しました（HTTP "_ju + juce::String (status) + "）"_ju);

    stream->readEntireStreamAsString();

    // 署名付き URL で直接送った場合は、サーバーにハッシュを検証してもらう
    if (! t.authRequired)
    {
        auto r = post ("/blobs/" + toJuce (t.hash) + "/complete", nlohmann::json::object());

        if (! r.ok())
            return juce::Result::fail (r.message());
    }

    return juce::Result::ok();
}

juce::Result SyncClient::download (const TransferUrl& t, juce::MemoryBlock& out) const
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
    stream->readIntoMemoryBlock (out);
    return juce::Result::ok();
}
