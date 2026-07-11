/*
 * xp_wellys_atc - AI-powered ATC voice communication for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "atc/sector_picker.hpp"

namespace sector_picker {

const airspace_db::Controller *pick_next(
    const std::vector<const airspace_db::Controller *> &enclosing,
    const std::vector<uint32_t> &visited_freqs_khz) {
  auto already_visited = [&](uint32_t khz) {
    for (uint32_t v : visited_freqs_khz)
      if (v == khz)
        return true;
    return false;
  };

  const airspace_db::Controller *best = nullptr;
  for (const auto *c : enclosing) {
    if (!c || c->freqs_khz.empty())
      continue;
    if (already_visited(c->freqs_khz.front()))
      continue;
    if (c->role == airspace_db::ControllerRole::TRACON) {
      if (!best || best->role != airspace_db::ControllerRole::TRACON ||
          c->floor_ft > best->floor_ft)
        best = c;
    } else if (c->role == airspace_db::ControllerRole::CTR) {
      if (!best || (best->role != airspace_db::ControllerRole::TRACON &&
                    c->floor_ft > best->floor_ft))
        best = c;
    }
  }
  return best;
}

} // namespace sector_picker
