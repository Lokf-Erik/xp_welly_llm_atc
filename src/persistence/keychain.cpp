/*
 * xp_wellys_atc - AI-powered ATC voice communication for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "persistence/keychain.hpp"

#include <cstring>

// Platform-specific headers must be at file scope, not inside a namespace.
#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <Security/Security.h>
#pragma clang diagnostic pop
#elif defined(__linux__)
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// Include order matters: <windows.h> must precede <wincred.h>.
// clang-format off
#include <windows.h>
#include <wincred.h> // Windows Credential Manager (Advapi32.lib)
// clang-format on
#endif

namespace persistence::keychain {

namespace {
constexpr const char *kProdService = "com.xp_wellys_atc.openai";
constexpr const char *kProdAccount = "default";
} // namespace

#if defined(__APPLE__)

bool save(const std::string &service, const std::string &account,
          const std::string &api_key) {
  if (api_key.empty())
    return false;

  SecKeychainItemRef item = nullptr;
  OSStatus status = SecKeychainFindGenericPassword(
      nullptr, static_cast<UInt32>(service.size()), service.c_str(),
      static_cast<UInt32>(account.size()), account.c_str(), nullptr, nullptr,
      &item);

  if (status == errSecSuccess && item) {
    status = SecKeychainItemModifyAttributesAndData(
        item, nullptr, static_cast<UInt32>(api_key.size()), api_key.c_str());
    CFRelease(item);
  } else {
    status = SecKeychainAddGenericPassword(
        nullptr, static_cast<UInt32>(service.size()), service.c_str(),
        static_cast<UInt32>(account.size()), account.c_str(),
        static_cast<UInt32>(api_key.size()), api_key.c_str(), nullptr);
  }
  return status == errSecSuccess;
}

std::string load(const std::string &service, const std::string &account) {
  UInt32 pw_len = 0;
  void *pw_data = nullptr;
  OSStatus status = SecKeychainFindGenericPassword(
      nullptr, static_cast<UInt32>(service.size()), service.c_str(),
      static_cast<UInt32>(account.size()), account.c_str(), &pw_len, &pw_data,
      nullptr);

  if (status == errSecSuccess && pw_data) {
    std::string result(static_cast<char *>(pw_data), pw_len);
    SecKeychainItemFreeContent(nullptr, pw_data);
    return result;
  }
  return {};
}

bool remove(const std::string &service, const std::string &account) {
  SecKeychainItemRef item = nullptr;
  OSStatus status = SecKeychainFindGenericPassword(
      nullptr, static_cast<UInt32>(service.size()), service.c_str(),
      static_cast<UInt32>(account.size()), account.c_str(), nullptr, nullptr,
      &item);

  if (status != errSecSuccess || !item)
    return false;

  status = SecKeychainItemDelete(item);
  CFRelease(item);
  return status == errSecSuccess;
}

bool has(const std::string &service, const std::string &account) {
  SecKeychainItemRef item = nullptr;
  OSStatus status = SecKeychainFindGenericPassword(
      nullptr, static_cast<UInt32>(service.size()), service.c_str(),
      static_cast<UInt32>(account.size()), account.c_str(), nullptr, nullptr,
      &item);
  if (status == errSecSuccess && item) {
    CFRelease(item);
    return true;
  }
  return false;
}

#elif defined(__linux__)

// File-based keychain: one file per (service, account) stored under
// ~/.config/xp_wellys_atc/ with permissions 0600.

namespace {

std::string key_path(const std::string &service, const std::string &account) {
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0')
    return {};

  std::string dir = std::string(home) + "/.config/xp_wellys_atc";
  mkdir(dir.c_str(), 0700);

  auto sanitize = [](std::string s) {
    for (auto &c : s)
      if (c == '/' || c == '\\' || c == ':')
        c = '_';
    return s;
  };

  return dir + "/" + sanitize(service) + "_" + sanitize(account) + ".key";
}

} // namespace

bool save(const std::string &service, const std::string &account,
          const std::string &api_key) {
  std::string path = key_path(service, account);
  if (path.empty())
    return false;
  int fd =
      ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
  if (fd < 0)
    return false;
  FILE *f = ::fdopen(fd, "w");
  if (!f) {
    ::close(fd);
    return false;
  }
  bool ok = std::fwrite(api_key.data(), 1, api_key.size(), f) == api_key.size();
  std::fclose(f);
  return ok;
}

std::string load(const std::string &service, const std::string &account) {
  std::string path = key_path(service, account);
  if (path.empty())
    return {};
  FILE *f = std::fopen(path.c_str(), "r");
  if (!f)
    return {};
  char buf[4096] = {};
  size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
  std::fclose(f);
  return std::string(buf, n);
}

bool remove(const std::string &service, const std::string &account) {
  std::string path = key_path(service, account);
  if (path.empty())
    return false;
  return unlink(path.c_str()) == 0;
}

bool has(const std::string &service, const std::string &account) {
  std::string path = key_path(service, account);
  if (path.empty())
    return false;
  return access(path.c_str(), F_OK) == 0;
}

#elif defined(_WIN32)

// Windows Credential Manager backend. Each (service, account) maps to a
// generic credential named "<service>/<account>" (e.g.
// "com.xp_wellys_atc.openai/default"), persisted per-machine. Credential
// Manager encrypts the blob via DPAPI under the hood — functionally
// equivalent to the Keychain's "no plaintext secret on disk" guarantee.

namespace {

std::wstring utf8_to_wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                              nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                      w.data(), n);
  return w;
}

std::wstring target_name(const std::string &service,
                         const std::string &account) {
  return utf8_to_wide(service + "/" + account);
}

} // namespace

bool save(const std::string &service, const std::string &account,
          const std::string &api_key) {
  if (api_key.empty())
    return false;
  std::wstring target = target_name(service, account);

  CREDENTIALW cred{};
  cred.Type = CRED_TYPE_GENERIC;
  cred.TargetName = const_cast<LPWSTR>(target.c_str());
  // Store the raw UTF-8 key bytes in the blob; load() returns them
  // verbatim. No wide conversion of the secret itself.
  cred.CredentialBlobSize = static_cast<DWORD>(api_key.size());
  cred.CredentialBlob =
      reinterpret_cast<LPBYTE>(const_cast<char *>(api_key.data()));
  cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

  return CredWriteW(&cred, 0) == TRUE;
}

std::string load(const std::string &service, const std::string &account) {
  std::wstring target = target_name(service, account);
  PCREDENTIALW cred = nullptr;
  if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || !cred)
    return {};
  std::string result(reinterpret_cast<char *>(cred->CredentialBlob),
                     cred->CredentialBlobSize);
  CredFree(cred);
  return result;
}

bool remove(const std::string &service, const std::string &account) {
  std::wstring target = target_name(service, account);
  return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == TRUE;
}

bool has(const std::string &service, const std::string &account) {
  std::wstring target = target_name(service, account);
  PCREDENTIALW cred = nullptr;
  if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) && cred) {
    CredFree(cred);
    return true;
  }
  return false;
}

#else

bool save(const std::string &, const std::string &, const std::string &) {
  return false;
}
std::string load(const std::string &, const std::string &) { return {}; }
bool remove(const std::string &, const std::string &) { return false; }
bool has(const std::string &, const std::string &) { return false; }

#endif

// Production-default convenience overloads.

bool save(const std::string &api_key) {
  return save(kProdService, kProdAccount, api_key);
}
std::string load() { return load(kProdService, kProdAccount); }
bool remove() { return remove(kProdService, kProdAccount); }
bool has() { return has(kProdService, kProdAccount); }

} // namespace persistence::keychain
