/*
 * xp_wellys_atc - AI-powered ATC voice communication for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef LOGGING_HPP
#define LOGGING_HPP

// printf-style format checking: GCC/Clang honour the attribute; MSVC has
// no equivalent, so it degrades to a no-op there.
#if defined(__GNUC__) || defined(__clang__)
#define XP_PRINTF_FORMAT(fmt_idx, args_idx)                                    \
  __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define XP_PRINTF_FORMAT(fmt_idx, args_idx)
#endif

namespace logging {

using Sink = void (*)(const char *);

// Plugin installs an XPLMDebugString wrapper in XPluginStart. Default sink
// writes to stderr so the engine module is usable from a headless test
// client before any sink is installed.
void set_sink(Sink s);

void debug(const char *fmt, ...) XP_PRINTF_FORMAT(1, 2);
void info(const char *fmt, ...) XP_PRINTF_FORMAT(1, 2);
void error(const char *fmt, ...) XP_PRINTF_FORMAT(1, 2);

} // namespace logging

#endif
