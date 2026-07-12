/*
 * xp_wellys_atc - AI-powered ATC voice communication for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef ATC_SECTOR_PICKER_HPP
#define ATC_SECTOR_PICKER_HPP

#include "data/airspace_db.hpp"

#include <cstdint>
#include <vector>

namespace sector_picker {

// Pick the next handoff sector from a set of enclosing controllers.
//
// Ranking (matches the poll_enroute sub-phase 1.5 and poll_approach
// sector-change pickers):
//   1. Prefer TRACON over CTR.
//   2. Among same role, prefer higher polygon floor (more specific).
//
// Backward-handoff guard: candidates whose front frequency (kHz) is in
// `visited_freqs_khz` are excluded — real ATC never hands back to a
// controller you were already handed OFF from earlier in the phase.
// This prevents the "Geneva -> Chambery -> Geneva" reversal seen in
// the Alps where Geneva TMA vertically overlaps Chambery TMA at the
// upper edge.
//
// Returns nullptr when no eligible candidate remains (e.g. every
// enclosing sector has been visited, or the aircraft has fully exited
// the airspace stack).
const airspace_db::Controller *pick_next(
    const std::vector<const airspace_db::Controller *> &enclosing,
    const std::vector<uint32_t> &visited_freqs_khz);

// Approach-phase variant of pick_next(). Same TRACON-over-CTR preference and
// backward-handoff guard, but the ranking is tuned for a DESCENDING aircraft:
//   1. Prefer TRACON over CTR.
//   2. Higher polygon floor (more specific) — same as pick_next.
//   3. Floor tie -> LOWER ceiling (the more terminal sector the aircraft has
//      just descended into). This is what drives Geneva (ceiling FL195) ->
//      Chambery (ceiling FL115) below FL115 at fields like LFLP where both
//      share floor 1000 and Geneva keeps enclosing all the way down; plain
//      pick_next (higher floor, first-seen tie) would stay on Geneva.
//   4. Ceiling tie -> nearest FACILITY airport to the destination (Chambery
//      LFLB over Lyon LFLL, both ceiling FL115). dest_lat/dest_lon = the
//      destination position; pass 0,0 to skip the facility tiebreak.
const airspace_db::Controller *pick_next_approach(
    const std::vector<const airspace_db::Controller *> &enclosing,
    const std::vector<uint32_t> &visited_freqs_khz,
    double dest_lat, double dest_lon);

// True if controller `a` represents a MORE TERMINAL (tighter) volume than `b`:
//   TRACON beats CTR, then higher floor, then lower ceiling.
// Used to decide whether an approach handoff is warranted: the aircraft should
// stay with its current controller while that controller still encloses it,
// UNLESS a more terminal enclosing volume appears (a descent into a tighter
// sector, e.g. Geneva -> Chambery). Prevents handing a still-enclosed aircraft
// off to a larger overlapping CTR (Chambery -> Lyon).
bool more_terminal(const airspace_db::Controller *a,
                   const airspace_db::Controller *b);

} // namespace sector_picker

#endif // ATC_SECTOR_PICKER_HPP
