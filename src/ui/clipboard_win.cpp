/*
 * xp_wellys_atc - AI-powered ATC voice communication for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

// Win32 clipboard bridge — the counterpart to clipboard.mm (macOS
// NSPasteboard) and clipboard_linux.cpp (wl-paste/xclip). Reads the
// system clipboard directly via the Win32 API; no external tool needed.
// Backs the Settings-tab [Paste] buttons because Ctrl+V is intercepted
// by X-Plane's command bindings.

#include "ui/clipboard.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

namespace ui::clipboard {

std::string read_system_text() {
  if (!OpenClipboard(nullptr))
    return {};

  std::string result;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  if (h) {
    auto *wide = static_cast<const wchar_t *>(GlobalLock(h));
    if (wide) {
      int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr,
                                  nullptr);
      if (n > 1) {
        std::vector<char> buf(static_cast<size_t>(n));
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, buf.data(), n, nullptr,
                            nullptr);
        // n includes the terminating NUL — drop it from the string.
        result.assign(buf.data(), static_cast<size_t>(n - 1));
      }
      GlobalUnlock(h);
    }
  }

  CloseClipboard();
  return result;
}

} // namespace ui::clipboard
