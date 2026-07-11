# IFR_ARRIVAL phase — implementation plan

**Status:** proposed (v4.4.0 candidate)
**Author:** design session 2026-07-11 (LIMF→LFLP retest)
**Depends on / relates to:** descent-phase ACC handoff gap, nearest_airport
refactor, P3-TRACON removal.

---

## 1. Problem

Today the state machine jumps straight from `IFR_DESCENT` to
`IFR_APPROACH_CONTACT` inside `poll_descent()` (`engine.cpp:4757–4766`)
the moment `fire_handoff` is true — triggered by an openair TMA/CTR
crossing **or** a crude **50 NM proximity fallback**. In the LIMF→LFLP
retest this fired at **30.5 NM / FL207**, while the aircraft was still:

- in the **France UIR** (LFFF, > FL195) — no Approach controller there,
- far from any TMA,
- not even at the first STAR fix.

So "approach phase" currently means *"within 50 NM of destination"*,
which is wrong. Consequences observed:

- No Approach controller found (P1 openair empty, P2 nearest-drift, P3
  removed) → silent transition → **the entire arrival ran with zero ATC
  voice** (no ACC handoff, no STAR step-downs, no check-in).

**Correct model (authoritative, per user):** issuing "descend FLxxx,
cleared via <STAR>, expect <approach>" is a **descent clearance from the
ACC** — still `IFR_DESCENT`. The approach phase begins only at a real
Approach (TMA) handoff. A distinct **arrival** phase — flying the STAR
under ACC/arrival control — sits between them.

---

## 2. Target phase model

```
IFR_ENROUTE_CRUISE   Center, cruise
  │  (pre-TOD / descent clearance: "descend FLxxx, cleared via STAR, expect APPCH")
  ▼
IFR_DESCENT          descending under ACC; ACC→ACC handoffs happen here
  │                  (LIMM Milan → France/LFFF UIR >FL195 → Marseille/LFMM FIR <FL195)
  │  TRIGGER A: cross the FIRST STAR fix   (STAR case)
  │  TRIGGER A': cross the IAF / first approach-transition fix   (no-STAR case)
  ▼
IFR_ARRIVAL   (NEW)  flying the STAR/transition under ACC/arrival;
  │                  STAR step-down clearances issued here
  │  TRIGGER B: real Approach (TMA/CTR) handoff to a found controller
  ▼
IFR_APPROACH_CONTACT Approach (Geneva/Chambéry); check-in, vectors/pilot-nav
  ▼
IFR_APPROACH_DESCENT → IFR_APPROACH_TOWER → IFR_LANDING_CLEARED   (unchanged)
```

Key point: **the 50 NM proximity heuristic is no longer an "enter
APPROACH" trigger.** It may survive only as a *last-resort* arrival
trigger when neither a STAR fix nor an approach fix is available (see §6).

---

## 3. State-machine changes (`atc_state_machine.{hpp,cpp}`)

1. **Enum:** add `IFR_ARRIVAL` immediately after `IFR_DESCENT` in
   `ATCState` (hpp:63). Keeps the enum ordering phase-chronological.
2. **`state_name()`:** add `case IFR_ARRIVAL: return "IFR/ARRIVAL";`.
3. **`state_from_name()` kMap:** add both `{"IFR/ARRIVAL", ...}` and
   `{"IFR_ARRIVAL", ...}`. **(This is the exact class of omission that
   caused the IFR/DESCENT→IDLE collapse — the round-trip test added in
   `test_state_history.cpp` will fail CI if it is forgotten.)**
4. **Reset lists:** no new persistent fields, so `init/stop/reset`
   need no change beyond the enum existing.

## 4. Templates (`data/atc_profiles/eu/ifr/atc_templates.json`)

Add a `towered."IFR/ARRIVAL"` block mirroring `IFR/DESCENT` (both live
only under `towered`; the lookup already falls back towered→ for IFR/*
states). Minimum keys: `READBACK` (silent ack, `next_state:
"IFR/ARRIVAL"`), `INITIAL_CALL_APPROACH` / `INITIAL_CALL_CENTER`
(sector check-in ack), `REQUEST_FREQUENCY`, `LEAVING_FREQUENCY`,
`_INVALID` (→ `IFR/ARRIVAL`, never IDLE). Copy the IFR/DESCENT block and
retarget `next_state`. **VFR templates untouched.**

## 5. Transition triggers

### 5a. `IFR_DESCENT → IFR_ARRIVAL`  (new)

Owner: `poll_descent()` (or a new helper it calls). Fire when the route
tracker reaches the **arrival entry fix**:

- **STAR case:** entry fix = first fix of the assigned STAR
  (`s_assigned_star_name` → CIFP; the entry fix is already known — it is
  the OFP last navlog fix, e.g. `SALEV`). Reuse the existing route
  tracker (`s_route_fixes` / `poll_route_tracker`) — fire when the
  aircraft passes (or is within ~1.5 NM of) the STAR entry fix index.
- Transition is **silent** (no "contact" — same ACC still owns the
  aircraft). Log `IFR descent -> arrival: crossed STAR entry fix <id>`.

### 5b. `IFR_ARRIVAL → IFR_APPROACH_CONTACT`  (was the 50 NM jump)

Owner: a new `poll_arrival()` (gated `state == IFR_ARRIVAL`). Fire ONLY
on a **real Approach handoff**: openair TMA/CTR enclosing entry **with a
resolved Approach controller** via `build_approach_handoff()`. If
`build_approach_handoff()` finds no controller, **stay in IFR_ARRIVAL**
(current sector keeps working the arrival) — do **not** silently jump to
APPROACH as line 4765 does today. The silent set_state at 4765 is
deleted; the "no dedicated approach" case is handled by staying in
ARRIVAL and letting `poll_approach`'s local INFO/AFIS-at-FAF handoff run
(see §6 no-STAR).

## 6. No-STAR / AFIS airports (REQUIRED — e.g. LFLP→LFQA, 4.2.1)

Some destinations have no STAR (`s_assigned_star_name` empty). The
existing no-STAR path loads approach fixes and the IAF closest to the
aircraft (`s_no_star_direct_iaf`, `engine.cpp:~1289`). Rules:

- **Arrival entry fix (Trigger A'):** when no STAR, use the **IAF /
  first approach-transition fix** as the `IFR_DESCENT → IFR_ARRIVAL`
  trigger. STAR step-downs (§7) simply have nothing to issue; ARRIVAL is
  then a short pass-through to the approach.
- **No STAR *and* no approach fixes** (pure AFIS direct-to-field): there
  is no fix to anchor on. Fall back to the **distance-based trigger**
  (the old 50 NM logic) but transition `IFR_DESCENT → IFR_ARRIVAL`
  (not APPROACH), and let `poll_approach`'s FAF-based INFO/AFIS handoff
  (`IFR_APPROACH_TOWER`) run as it does today. This preserves the
  validated LFQA behaviour — only the state label changes (ARRIVAL
  instead of premature APPROACH_CONTACT).
- **Guard:** never skip ARRIVAL for a STAR airport just because the
  Approach controller lookup fails — that was the LFLP silent-arrival
  bug. ARRIVAL must be reachable independently of controller resolution.

## 7. STAR step-down clearances move into ARRIVAL

The STAR-constraint walker (`build_star_constraint`, the
`s_approach_waypoints` step-down logic currently in `poll_approach`)
conceptually belongs to the **arrival** phase (ACC issues "descend
FL090" at LUVOB before any Approach handoff). Move the STAR step-down
issuance so it runs in `IFR_ARRIVAL` (`poll_arrival`), leaving
`poll_approach` to own only post-Approach-handoff behaviour (check-in
ack, vectors/pilot-nav to FAF, Tower handoff). This also fixes the
observed "no step-downs during descent" symptom. (Keep the known STAR
walker 80/20 + STAR-entry-altitude heuristics in mind — those are
tracked separately and can be addressed in the same move.)

## 8. Controller-label / ACC-handoff interaction

- The **LIMM→France/Marseille ACC handoff** is a *separate* work item
  (descent-phase ACC handoff gap). It fires in `IFR_DESCENT` and should
  also fire in `IFR_ARRIVAL` (both are "under ACC"). When implemented,
  `poll_descent` and `poll_arrival` share the sector-change mechanism
  (poll_enroute sub-phase 1.5 + `sector_picker` visited-guard).
- `s_current_controller_label` must survive the DESCENT→ARRIVAL
  transition (no reset) so the arrival controller label is continuous
  until a real handoff swaps it.

## 9. Edge cases & risks

- **Direct-to past the STAR entry fix:** if the aircraft is already
  inside/past the entry fix when the descent clearance fires (short
  sector), fire Trigger A immediately (index-passed check, like the
  route tracker's existing `start idx`).
- **Route tracker index sync:** the entry-fix index must be resolved
  against `s_route_fixes` (it already contains STAR + approach fixes —
  log showed `tracker init: 17 fixes`).
- **Training-jump entry** (`training_jump_*`): must seed the phase
  correctly — a jump straight into descent/arrival must set the right
  state, not default to IFR_DESCENT then never advance.
- **Reset on go-around / missed:** ARRIVAL is not re-entered on a
  missed approach (that stays in the APPROACH/GO_AROUND family).

## 10. Tests

- **Round-trip test** already covers the new enum value (guaranteed by
  the loop in `test_state_history.cpp`).
- Scenario (`atc_repl`): a STAR arrival must visit
  `IFR_DESCENT → IFR_ARRIVAL → IFR_APPROACH_CONTACT` in order; assert
  the ARRIVAL entry coincides with the STAR entry fix, not a distance.
- No-STAR scenario (LFQA-style): must reach `IFR_ARRIVAL` then
  `IFR_APPROACH_TOWER` (AFIS) without ever silently landing in
  `IFR_APPROACH_CONTACT` at 50 NM.
- Assert the old silent `set_state(IFR_APPROACH_CONTACT)` at
  `poll_descent` 4765 is gone.

## 11. Out of scope (separate items)

- The LIMM→France/Marseille **ACC→ACC handoff itself** (descent-phase
  sector handoff) — companion work, referenced in §8.
- STAR walker 80/20 firing + STAR-entry-altitude heuristic (P0s tracked
  elsewhere) — touched only incidentally by the §7 move.

## 12. Rollout

Compiles in the SDK-free engine lib; testable headless via `atc_repl` +
scenario scripts before any in-sim run. Ship behind the normal build;
validate in-sim on one STAR arrival (LIMF→LFLP) and one no-STAR arrival
(LFLP→LFQA) before tagging. Commit only after both fly end-to-end.
