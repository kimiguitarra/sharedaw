#include "CredentialStore.h"

#include "AppPaths.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <wincred.h>
 #pragma comment (lib, "Advapi32.lib")
#elif JUCE_MAC
 #include <Security/Security.h>
#else
 #include <sys/stat.h>
#endif

namespace CredentialStore
{

namespace
{
   #if JUCE_WINDOWS || JUCE_MAC
    juce::String targetName (const juce::String& serverUrl)
    {
        return "ShareDAW:" + serverUrl.trimCharactersAtEnd ("/");
    }
   #endif

   #if ! JUCE_WINDOWS && ! JUCE_MAC
    juce::File tokenFile (const juce::String& serverUrl)
    {
        return AppPaths::getAppDataDir().getChildFile ("tokens")
                 .getChildFile (juce::String::toHexString (serverUrl.hashCode64()) + ".token");
    }
   #endif
}

bool saveToken (const juce::String& serverUrl, const juce::String& token)
{
   #if JUCE_WINDOWS
    auto target = targetName (serverUrl);
    auto utf8 = token.toStdString();

    CREDENTIALW cred {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR> (target.toWideCharPointer());
    cred.CredentialBlobSize = (DWORD) utf8.size();
    cred.CredentialBlob = reinterpret_cast<LPBYTE> (utf8.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<LPWSTR> (L"ShareDAW");
    return CredWriteW (&cred, 0) != FALSE;
   #elif JUCE_MAC
    removeToken (serverUrl);
    auto service = targetName (serverUrl).toStdString();
    auto utf8 = token.toStdString();

    CFStringRef serviceRef = CFStringCreateWithCString (nullptr, service.c_str(), kCFStringEncodingUTF8);
    CFDataRef data = CFDataCreate (nullptr, reinterpret_cast<const UInt8*> (utf8.data()), (CFIndex) utf8.size());

    const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData };
    const void* values[] = { kSecClassGenericPassword, serviceRef, CFSTR ("ShareDAW"), data };
    CFDictionaryRef query = CFDictionaryCreate (nullptr, keys, values, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    const auto status = SecItemAdd (query, nullptr);
    CFRelease (query);
    CFRelease (data);
    CFRelease (serviceRef);
    return status == errSecSuccess;
   #else
    auto f = tokenFile (serverUrl);
    f.getParentDirectory().createDirectory();

    if (! f.replaceWithText (token))
        return false;

    ::chmod (f.getFullPathName().toRawUTF8(), 0600);
    return true;
   #endif
}

juce::String loadToken (const juce::String& serverUrl)
{
   #if JUCE_WINDOWS
    PCREDENTIALW cred = nullptr;

    if (! CredReadW (targetName (serverUrl).toWideCharPointer(), CRED_TYPE_GENERIC, 0, &cred))
        return {};

    auto token = juce::String::fromUTF8 (reinterpret_cast<const char*> (cred->CredentialBlob), (int) cred->CredentialBlobSize);
    CredFree (cred);
    return token;
   #elif JUCE_MAC
    auto service = targetName (serverUrl).toStdString();
    CFStringRef serviceRef = CFStringCreateWithCString (nullptr, service.c_str(), kCFStringEncodingUTF8);

    const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit };
    const void* values[] = { kSecClassGenericPassword, serviceRef, CFSTR ("ShareDAW"), kCFBooleanTrue, kSecMatchLimitOne };
    CFDictionaryRef query = CFDictionaryCreate (nullptr, keys, values, 5, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    CFTypeRef result = nullptr;
    const auto status = SecItemCopyMatching (query, &result);
    CFRelease (query);
    CFRelease (serviceRef);

    if (status != errSecSuccess || result == nullptr)
        return {};

    auto data = (CFDataRef) result;
    auto token = juce::String::fromUTF8 (reinterpret_cast<const char*> (CFDataGetBytePtr (data)), (int) CFDataGetLength (data));
    CFRelease (result);
    return token;
   #else
    return tokenFile (serverUrl).loadFileAsString().trim();
   #endif
}

void removeToken (const juce::String& serverUrl)
{
   #if JUCE_WINDOWS
    CredDeleteW (targetName (serverUrl).toWideCharPointer(), CRED_TYPE_GENERIC, 0);
   #elif JUCE_MAC
    auto service = targetName (serverUrl).toStdString();
    CFStringRef serviceRef = CFStringCreateWithCString (nullptr, service.c_str(), kCFStringEncodingUTF8);
    const void* keys[] = { kSecClass, kSecAttrService, kSecAttrAccount };
    const void* values[] = { kSecClassGenericPassword, serviceRef, CFSTR ("ShareDAW") };
    CFDictionaryRef query = CFDictionaryCreate (nullptr, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    SecItemDelete (query);
    CFRelease (query);
    CFRelease (serviceRef);
   #else
    tokenFile (serverUrl).deleteFile();
   #endif
}

}
