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

} // namespace sector_picker

#endif // ATC_SECTOR_PICKER_HPP
