/*
 * xp_wellys_atc - AI-powered ATC voice communication for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "audio/mic_permission.hpp"

namespace mic_permission {

// Windows 10+ has no in-process microphone permission prompt like macOS
// TCC. App mic access is governed by the system Privacy settings; a
// blocked device simply yields an empty capture stream (handled in the
// WASAPI capture path). No-op, mirroring the Linux stub.
bool check_and_request() { return true; }

} // namespace mic_permission
