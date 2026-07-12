/*
 * xp_wellys_atc - AI-powered ATC voice communication for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "atc/sector_picker.hpp"

#include "core/xplane_context.hpp"
#include "data/traffic_geometry.hpp"

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

const airspace_db::Controller *pick_next_approach(
    const std::vector<const airspace_db::Controller *> &enclosing,
    const std::vector<uint32_t> &visited_freqs_khz,
    double dest_lat, double dest_lon) {
  auto already_visited = [&](uint32_t khz) {
    for (uint32_t v : visited_freqs_khz)
      if (v == khz)
        return true;
    return false;
  };
  const bool have_dest = (dest_lat != 0.0 || dest_lon != 0.0);
  // Facility-airport distance to the destination (tiebreak #4). Falls back to a
  // large value when the position is unavailable (headless stub / no facility).
  auto facility_dist = [&](const airspace_db::Controller *c) -> double {
    if (!have_dest || c->facility_id.empty())
      return 1e18;
    auto p = xplane_context::airport_pos_for(c->facility_id);
    if (p.first == 0.0 && p.second == 0.0)
      return 1e18;
    return traffic_geometry::distance_nm(dest_lat, dest_lon, p.first, p.second);
  };
  auto ceil_of = [](const airspace_db::Controller *c) {
    return c->ceiling_ft > 0 ? c->ceiling_ft : 999999; // 0 = unbounded
  };

  const airspace_db::Controller *best = nullptr;
  for (const auto *c : enclosing) {
    if (!c || c->freqs_khz.empty())
      continue;
    if (already_visited(c->freqs_khz.front()))
      continue;
    if (c->role != airspace_db::ControllerRole::TRACON &&
        c->role != airspace_db::ControllerRole::CTR)
      continue;
    if (!best) {
      best = c;
      continue;
    }
    const bool c_tracon = c->role == airspace_db::ControllerRole::TRACON;
    const bool b_tracon = best->role == airspace_db::ControllerRole::TRACON;
    bool better;
    if (c_tracon != b_tracon)
      better = c_tracon; // TRACON beats CTR
    else if (c->floor_ft != best->floor_ft)
      better = c->floor_ft > best->floor_ft; // higher floor = more specific
    else if (ceil_of(c) != ceil_of(best))
      better = ceil_of(c) < ceil_of(best); // lower ceiling = more terminal
    else
      better = facility_dist(c) < facility_dist(best); // nearer facility
    if (better)
      best = c;
  }
  return best;
}

bool more_terminal(const airspace_db::Controller *a,
                   const airspace_db::Controller *b) {
  if (!a) return false;
  if (!b) return true;
  const bool a_tracon = a->role == airspace_db::ControllerRole::TRACON;
  const bool b_tracon = b->role == airspace_db::ControllerRole::TRACON;
  if (a_tracon != b_tracon)
    return a_tracon; // TRACON beats CTR
  if (a->floor_ft != b->floor_ft)
    return a->floor_ft > b->floor_ft; // higher floor = more specific
  auto ceil_of = [](const airspace_db::Controller *c) {
    return c->ceiling_ft > 0 ? c->ceiling_ft : 999999; // 0 = unbounded
  };
  return ceil_of(a) < ceil_of(b); // lower ceiling = more terminal
}

} // namespace sector_picker
