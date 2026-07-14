// Catch2 unit tests for sector_picker::pick_next.
//
// The picker feeds both poll_enroute sub-phase 1.5 and the
// poll_approach sector-change block. Its job is to choose the next
// handoff controller from the set of enclosing polygons, ranked by:
//   1. TRACON over CTR
//   2. Higher polygon floor within same role
//   3. Never re-elect a sector that was previously handed off from
//      (backward-handoff guard — Geneva-over-Chambery pathology).
//
// Fixtures are inline stack-allocated Controller structs — no
// airspace_db state, no atc.dat parsing.

#include "atc/sector_picker.hpp"
#include "data/airspace_db.hpp"

#include <catch2/catch_amalgamated.hpp>

using airspace_db::Controller;
using airspace_db::ControllerRole;

static Controller make_ctrl(const std::string &name, ControllerRole role,
                            uint32_t freq_khz, int floor_ft) {
  Controller c;
  c.name = name;
  c.role = role;
  c.freqs_khz.push_back(freq_khz);
  c.floor_ft = floor_ft;
  return c;
}

TEST_CASE("sector_picker: empty enclosing returns nullptr", "[sector_picker]") {
  std::vector<const Controller *> enclosing;
  CHECK(sector_picker::pick_next(enclosing, {}) == nullptr);
}

TEST_CASE("sector_picker: single TRACON wins", "[sector_picker]") {
  auto lyon = make_ctrl("LYON", ControllerRole::TRACON, 121400, 3000);
  std::vector<const Controller *> enclosing = {&lyon};
  auto *best = sector_picker::pick_next(enclosing, {});
  REQUIRE(best != nullptr);
  CHECK(best->name == "LYON");
}

TEST_CASE("sector_picker: TRACON preferred over CTR", "[sector_picker]") {
  auto france = make_ctrl("FRANCE", ControllerRole::CTR, 118030, 19500);
  auto chambery = make_ctrl("CHAMBERY", ControllerRole::TRACON, 121205, 3000);
  std::vector<const Controller *> enclosing = {&france, &chambery};
  auto *best = sector_picker::pick_next(enclosing, {});
  REQUIRE(best != nullptr);
  CHECK(best->name == "CHAMBERY");
}

TEST_CASE("sector_picker: higher floor wins within same role",
          "[sector_picker]") {
  auto chambery = make_ctrl("CHAMBERY", ControllerRole::TRACON, 121205, 3000);
  auto geneva   = make_ctrl("GENEVA",   ControllerRole::TRACON, 119530, 8000);
  std::vector<const Controller *> enclosing = {&chambery, &geneva};
  auto *best = sector_picker::pick_next(enclosing, {});
  REQUIRE(best != nullptr);
  CHECK(best->name == "GENEVA");
}

// ── The regression case: Geneva-over-Chambery backward-handoff ───────
// Sequence observed in the 2026-07-08 LIMF -> LFLP retest:
//   1. enclosing = [GENEVA]                     -> pick GENEVA (seed)
//   2. enclosing = [CHAMBERY] (Geneva exited)   -> pick CHAMBERY (handoff)
//   3. enclosing = [GENEVA, CHAMBERY] (aircraft
//      moved horizontally back into Geneva)    -> MUST NOT re-elect GENEVA.
// With the visited-guard populated after step 2, step 3 must return
// CHAMBERY (unchanged) — real ATC never hands back.

TEST_CASE("sector_picker: backward handoff blocked by visited list",
          "[sector_picker]") {
  auto chambery = make_ctrl("CHAMBERY", ControllerRole::TRACON, 121205, 3000);
  auto geneva   = make_ctrl("GENEVA",   ControllerRole::TRACON, 119530, 8000);

  // Step 3: both enclose, but Geneva was handed off from at step 2.
  std::vector<const Controller *> enclosing = {&geneva, &chambery};
  std::vector<uint32_t> visited = {119530}; // Geneva.

  auto *best = sector_picker::pick_next(enclosing, visited);
  REQUIRE(best != nullptr);
  CHECK(best->name == "CHAMBERY");
  CHECK(best->freqs_khz.front() == 121205u);
}

TEST_CASE("sector_picker: all candidates visited returns nullptr",
          "[sector_picker]") {
  auto chambery = make_ctrl("CHAMBERY", ControllerRole::TRACON, 121205, 3000);
  auto geneva   = make_ctrl("GENEVA",   ControllerRole::TRACON, 119530, 8000);
  std::vector<const Controller *> enclosing = {&geneva, &chambery};
  std::vector<uint32_t> visited = {119530, 121205};
  CHECK(sector_picker::pick_next(enclosing, visited) == nullptr);
}

// Legitimate forward flow: Lyon -> Chambery -> Tower. After Lyon is
// visited, the Chambery pick must still be selectable and unaffected
// by the guard.

TEST_CASE("sector_picker: forward Lyon -> Chambery still works",
          "[sector_picker]") {
  auto lyon     = make_ctrl("LYON",     ControllerRole::TRACON, 121400, 5000);
  auto chambery = make_ctrl("CHAMBERY", ControllerRole::TRACON, 121205, 3000);

  // First tick after Lyon->Chambery handoff would have added Lyon to
  // visited. New enclosing may still hold both.
  std::vector<const Controller *> enclosing = {&lyon, &chambery};
  std::vector<uint32_t> visited = {121400}; // Lyon.

  auto *best = sector_picker::pick_next(enclosing, visited);
  REQUIRE(best != nullptr);
  CHECK(best->name == "CHAMBERY");
}

TEST_CASE("sector_picker: skips controller with no frequency",
          "[sector_picker]") {
  Controller empty_ctrl;
  empty_ctrl.name = "NO_FREQ";
  empty_ctrl.role = ControllerRole::TRACON;
  empty_ctrl.floor_ft = 10000; // Would otherwise win by floor.
  auto chambery = make_ctrl("CHAMBERY", ControllerRole::TRACON, 121205, 3000);
  std::vector<const Controller *> enclosing = {&empty_ctrl, &chambery};
  auto *best = sector_picker::pick_next(enclosing, {});
  REQUIRE(best != nullptr);
  CHECK(best->name == "CHAMBERY");
}

// ── pick_next_approach: lower-ceiling tiebreak (Geneva -> Chambery) ─────────
// Regression for the LFLP arrival: Geneva (floor 1000, ceiling FL195) and
// Chambery (floor 1000, ceiling FL115) both enclose the field below FL115.
// pick_next (higher floor, first-seen tie) stays on Geneva forever, so the
// Geneva -> Chambery handoff never fires. pick_next_approach breaks the floor
// tie by the LOWER ceiling -> Chambery, the more terminal sector.
static Controller make_ctrl_fc(const std::string &name, ControllerRole role,
                               uint32_t freq_khz, int floor_ft, int ceil_ft) {
  Controller c;
  c.name = name;
  c.role = role;
  c.freqs_khz.push_back(freq_khz);
  c.floor_ft = floor_ft;
  c.ceiling_ft = ceil_ft;
  return c;
}

TEST_CASE("sector_picker: approach prefers lower ceiling on floor tie",
          "[sector_picker]") {
  auto geneva = make_ctrl_fc("GENEVA", ControllerRole::TRACON, 119530, 1000, 19500);
  auto chambery = make_ctrl_fc("LYON", ControllerRole::TRACON, 121205, 1000, 11500);
  // Geneva listed first (as atc.dat record order would have it).
  std::vector<const Controller *> enclosing = {&geneva, &chambery};

  // Plain pick_next: floor tie -> first-seen -> Geneva (the bug).
  CHECK(sector_picker::pick_next(enclosing, {})->name == "GENEVA");

  // Approach pick: lower ceiling -> Chambery (LYON/LFLB record). dest pos 0,0
  // skips the facility tiebreak (not needed to separate ceilings here).
  auto *best = sector_picker::pick_next_approach(enclosing, {}, 0.0, 0.0);
  REQUIRE(best != nullptr);
  CHECK(best->ceiling_ft == 11500);
  CHECK(best->freqs_khz.front() == 121205);
}

TEST_CASE("sector_picker: approach still honors higher floor first",
          "[sector_picker]") {
  // When floors differ, higher floor wins regardless of ceiling (same as
  // pick_next) -- the lower-ceiling rule only breaks floor ties.
  auto low = make_ctrl_fc("A", ControllerRole::TRACON, 100000, 1000, 9000);
  auto high = make_ctrl_fc("B", ControllerRole::TRACON, 100100, 5000, 19500);
  std::vector<const Controller *> enclosing = {&low, &high};
  auto *best = sector_picker::pick_next_approach(enclosing, {}, 0.0, 0.0);
  REQUIRE(best != nullptr);
  CHECK(best->name == "B");
}

TEST_CASE("sector_picker: approach backward-handoff guard holds",
          "[sector_picker]") {
  auto geneva = make_ctrl_fc("GENEVA", ControllerRole::TRACON, 119530, 1000, 19500);
  auto chambery = make_ctrl_fc("LYON", ControllerRole::TRACON, 121205, 1000, 11500);
  std::vector<const Controller *> enclosing = {&geneva, &chambery};
  // Already handed off from Chambery -> must not re-elect it; falls to Geneva.
  auto *best = sector_picker::pick_next_approach(enclosing, {121205}, 0.0, 0.0);
  REQUIRE(best != nullptr);
  CHECK(best->name == "GENEVA");
}
