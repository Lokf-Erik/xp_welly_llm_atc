# IFR & Linux Roadmap

This file tracks the IFR flight simulation feature set and the Linux port status
for the `xp_wellys_atc` fork. VFR features are maintained upstream.

Last synced with the code: **v4.2.1** (commit `83be311`, released 2026-07-05,
tag `v4.2.1`) — plus the 4.2.2 WIP items below.

---

## Recent release milestones

| Version | Headline | Date |
|---------|----------|------|
| **v4.2.1** | Full implementation and test of NON-STAR / AFIS-ONLY airport arrivals (IFR LFLP → LFQA, RNAV RWY 07 validated end-to-end) | 2026-07-05 |
| v4.2.0 | IFR engine improvements, airspace fixes, STT accuracy | 2026-06-27 |
| v4.1.4 | Voxtral `context_bias` + `initial_prompt` enrichment, q5_1 CPU crash fix, GPU VRAM detection | 2026-06-26 |

---

## Phase 1 — Linux port (Ubuntu 24.04 / Zorin 18) — DONE

- PipeWire / PulseAudio mic capture (`pa_simple`)
- OpenSSL EVP SHA256 (replaces CommonCrypto)
- `$ORIGIN` rpath, `lin_x64` install directory
- `libXPLM_64.so` extracted via `make setup`
- Transponder DataRefs (`transponder_code`, `transponder_mode`)

---

## Phase 2 — IFR Ground Operations

### 2.1 SimBrief integration — 85%

- [x] Async OFP fetch + parse (destination, SID, cruise FL, registration, type)
- [x] Full navlog parsed (`NavlogFix`: ident, lat/lon, airway, alt, SID/STAR flag)
- [x] IFR tab: Pilot ID, `[Fetch OFP]`, route summary, scrollable FPL waypoint list
- [x] `ctx.ifr_destination` populated from OFP
- [ ] Slot-time check: warn if sim time is off the filed `sched_off`
- [ ] Block `REQUEST_IFR_CLEARANCE` when destination is empty
- [ ] `[Clear OFP]` button for new flight
- [ ] Local `.fms` fallback when SimBrief is offline (~30 lines)
- [ ] Manual FPL entry UI (dest ICAO + cruise FL + `[Set FPL]`) (~50 lines)

### 2.2 Clearance + Startup — 90%

- [x] ATIS information letter challenge before clearance
- [x] Clearance: squawk (random in configured range) + SID (CIFP) + initial altitude + destination
- [x] Engine start approval
- [x] Tower-only airports (no separate Delivery/Ground controller)
- [x] CIFP binding minimum altitude (`ifr_sid_min_alt_ft` / `ifr_sid_min_waypoint`)
- [ ] Re-clearance when pilot requests below CIFP binding minimum

### 2.3 Taxiing + Departing — 95%

- [x] Taxi clearance with passive squawk reminder ("verify squawk XXXX mode Charlie")
- [x] Squawk check at holding point: active mode C + correct code verified
- [x] Line-up-and-wait → takeoff clearance
- [x] Takeoff clearance: wind stated (not read back) + "passing Xft contact Approach on Y.YYY"
- [x] Tower → Departure/Approach freq handoff (`IFR_FREQ_HANDOFF`); pilot reads back
- [x] Departure check-in → `IFR_RADAR_CONTACT`; ATC issues SID step climbs
- [x] Direct-to last SID fix + step1 FL + cruise FL clearances via `poll_sid_climb()`
- [x] Radar handoff at TMA upper boundary (openair_db) → Centre (`IFR_ENROUTE_CRUISE`)
- [x] Controller name + frequency from `atc.dat` TRACON at 3-D aircraft position
- [ ] Departure re-clearance enforcing CIFP binding minimum

---

## Phase 3 — En-route — 65%

- [x] `IFR_ENROUTE_CRUISE` state: pilot on Centre, no SID step-climb re-trigger
- [x] Centre check-in (`INITIAL_CALL_APPROACH`): "radar contact." — stays in cruise state
- [x] En-route direct-to shortcut: "N111RC, direct XAMUR, when able."
      (fired ~90-120 s after Centre check-in; first non-SID/STAR navlog fix >20 NM ahead)
- [x] Proactive descent clearance on destination TMA entry (no pilot request needed):
      openair_db CTA/UIR→TMA class transition triggers
      "N111RC, descend flight level 80, contact Nice Approach on 120.350."
      → `IFR_APPROACH_CONTACT`
- [x] Cross-track deviation alert: "confirm routing, you appear off track."
      (>5 NM off filed navlog, 3-minute cooldown)
- [x] Altitude deviation warning: unit-consistent (FL vs pressure altitude, feet vs MSL),
      RVSM 200 ft above FL290, 300 ft below. 2-min cooldown, 60 s grace after check-in.
- [x] Sector-change (en-route + approach) updates `s_enroute_approach_freq_mhz`
      when the new sector is a TRACON, so check-in fires only on the new frequency
- [x] Sector-change suppresses redundant "contact X on Y" when pilot is already tuned
- [x] Sector check-in mandatory ack ("radar contact") — never silent
- [x] IFR speed restriction below FL100 (`poll_speed_restriction`, 250 kt / IAS>255)
- [ ] Centre altitude reassignments (level-off, step-climb during cruise)
- [ ] En-route traffic separation (speed / heading / altitude adjustments)
- [ ] **Pilot deviation requests (for weather)** — pilot-initiated intent
      `REQUEST_DEVIATION` ("request 20 right for weather", "request deviation
      due to buildups"). ATC either approves ("deviation approved, report
      back on course") or negotiates ("unable, traffic — try 10 right"). Must
      unlock the cross-track deviation warning while the deviation is
      approved, and re-arm it once the pilot reports back on course.
- [ ] **Pilot level-change requests (turbulence / ride quality)** — pilot-
      initiated intents `REQUEST_HIGHER` / `REQUEST_LOWER` ("request FL350
      for smoother ride", "request FL290 due to turbulence", "request lower,
      moderate chop"). ATC approves ("climb FL350"), offers an alternative
      ("unable FL350, FL330 available"), or refuses ("unable, traffic at
      FL350, maintain FL310"). Must update `s_enroute_cleared_alt_ft` so the
      altitude-deviation warning tracks the new target.
- [ ] Destination airport change mid-flight → automatic transition to Approach contact
- [ ] TMA descent fallback when `airspace.txt` is absent
- [ ] Intra-ACC sector frequency changes (Marseille has ~12 sectors; plugin currently
      uses only the primary channel — sub-sector polygon data absent from atc.dat)

### 3.5 CIFP SID/STAR constraints — not started (important)

Parsed altitude and speed constraint fields per waypoint. Required for
Phase 4 STAR descent enforcement to be fully realistic.

- [ ] `cifp_reader`: parse altitude constraint type per waypoint
      (`+` = at-or-above, `-` = at-or-below, `B` = block-between)
- [ ] `cifp_reader`: feed the per-waypoint speed constraint into enforcement
      (already extracted as `StarWaypoint.speed_kt`)
- [ ] SID enforcement: `poll_sid_climb` respects the SID waypoint ceiling
- [ ] STAR enforcement: fix-by-fix "descend to FL100, cross XAMUR at FL080"
- [ ] `ctx.ifr_sid_constraints` / `ifr_star_constraints` vectors

### 3.6 STAR/Approach deviation monitoring — not started (important)

- [ ] Next-fix heading check: bearing to next assigned STAR fix vs
      `ctx.heading_true`. If deviation >30° for >60 s: "confirm routing,
      you appear to be deviating from [FIX]." 2-min cooldown.
- [ ] TL-aware FL / feet+QNH format (`ctx.transition_alt_ft` + QNH → dynamic TL)

---

## Phase 4 — Approach + Landing — ~65% (major progress in v4.2.1)

**Shipped in v4.1.x / v4.2.x:**

- [x] `INITIAL_CALL_APPROACH` intercept: "radar contact, identified, direct [IAF],
      [type] approach runway [N], descend [alt], QNH [Q]"
- [x] STAR step-down clearances (altitude-triggered per waypoint,
      `poll_approach` STAR loop)
- [x] `s_assigned_star_name` / `s_assigned_dest_icao` / `s_assigned_approach_designator`
      wired through the descent clearance
- [x] Approach type + variant handling (RNAV Z, ILS Y, RNP)
- [x] `cifp_reader::approach_procedure_waypoints()` — IAF-transition + body records
      (FM vectoring + IF entry filtered; R-type FAF supported for RNAV)
- [x] `cifp_reader::star_last_fix()` — last STAR fix used as IAF connector
- [x] `cifp_reader::approach_faf()` — accepts route_type "I" (ILS) and "R" (RNAV)
- [x] Route tracker (`s_route_fix_idx`, `s_faf_route_idx`, `s_iaf_route_idx`)
      with dual-use IAF/MAP-hold pattern handling (LFQA RNAV R07/R25 fix)
- [x] Direct-to-IAF from descent clearance: tracker jumps to that fix so
      intermediate skipped waypoints don't stall the FAF handoff
- [x] Distance-to-FAF fallback (parallel to route tracker) so vectored
      approaches that don't overfly every intermediate fix still trigger
- [x] FAF Tower/AFIS handoff: `s_current_controller_label` updated to the new
      facility (airport NAME, not ICAO — "Reims Prunay Information" not
      "LFQA Information")
- [x] Landing clearance uses the CIFP-assigned runway (`set_assigned_runway`),
      not the wind-favoured active runway — Tower issues RWY 07 for the R07
      approach even on calm days
- [x] Landing clearance: wind + runway + "cleared to land"
- [x] Approach check-in state gate accepts `IFR/APPROACH_CONTACT` and
      `IFR/DESCENT` (frequency-authoritative)
- [x] Broadened check-in intent set: any `INITIAL_CALL_*` variant or `UNKNOWN`
      on the approach freq fires the full clearance (garbled facility names OK)
- [x] Broadened Tower/AFIS check-in — "Reims Prunay Information" and Voxtral-
      garbled forms trigger the landing clearance
- [x] Unit-consistent altitude comparisons: FL against pressure altitude,
      feet (QNH) against altitude MSL — for `initial_ft`, `no_descent_needed`,
      floor skip, and target-selection loops
- [x] "Continue descent to X" fires only when Approach's target equals Centre's
      last cleared altitude; a new (lower) target uses "descend X"
- [x] Verify-descending soft prompt ("confirm descending [alt]") 45–100 s after
      a clearance when |VS| < 200 fpm and 500–800 ft off target — fires before
      the hard-deviation warning

**Still open:**

- [ ] Missed approach / go-around: "fly runway heading, climb [alt] feet"
      (new intent `GOING_AROUND_IFR`)
- [ ] Destination ATIS challenge at Approach check-in ("information Zulu")
- [ ] Destination active-runway computation for STAR/approach selection
      (currently CIFP + wind heuristic)

**Traffic separation directives (future — TCAS-driven):**

- [ ] Speed adjustment: "reduce to 180 knots" / "speed your discretion"
- [ ] Heading vectors: "fly heading 090, vectors to ILS"
- [ ] Distance/time-based sequencing: "number 2 for the ILS, follow traffic ahead"
- [ ] Go-around due to traffic: "go around, traffic on the runway"

---

## Phase 5 — Post-landing — ~40% (v4.2.1 landed the AFIS path)

**Shipped in v4.2.1:**

- [x] Runway vacated → contact Ground (towered) OR taxi via nearest edge
      (tower-only / AFIS destinations)
- [x] `nearest_taxiway_phrase()` uses point-to-segment distance against every
      apt.dat 1202 taxiway edge — picks the taxiway the aircraft is
      physically ON, not the closest midpoint (which could sit on a parallel
      taxiway across the runway)
- [x] IFR flight plan closure at destination:
      - AFIS → "leaving frequency approved, contact [airport] by telephone
        to close IFR flight plan, good day."
      - Towered → "IFR flight plan closed at HHMM, good day."
- [x] `RUNWAY_VACATED_TOWER_ONLY` now arms readback so the taxi + "report on
      stand" instruction must be read back

**Still open:**

- [ ] Engine shutdown acknowledgement
- [ ] Parking stand assignment (apt.dat 1300 aircraft category: A=GA, B=turboprop,
      C=narrow-body, D=wide-body, E=heavy)
- [ ] Dijkstra taxi routing on apt.dat 1201/1202 node/edge graph (proper multi-
      segment "taxi via A, B, C" routes — current output is the single nearest
      edge, sufficient for most GA fields but not big hubs)

---

## Under active work — v4.2.2 (WIP)

- [x] IFR speed restriction now **continuously enforced** below FL100:
      the "once per descent" flag was too permissive — if the pilot slowed to
      240 kt then accelerated back to 260 kt, no further advisory fired.
      Flag now also resets when the pilot complies (IAS ≤ 245 kt, 5 kt
      hysteresis below 250) so any subsequent overspeed re-fires.
- [x] PTT release tail extended from 600 ms → 900 ms (`kPttTailSec` in
      `atc_session.cpp`) — Voxtral was still truncating trailing callsigns
      ("...Romeo Charlie") when the pilot let the key go while finishing the
      final syllable.
- [ ] **Known follow-up:** re-press PTT during the tail window is dropped
      (state != IDLE gate blocks it). Cancel the tail cleanly and continue
      as one session, or start a fresh one.

---

## Planned — v4.3.0

- **SID/STAR waypoint speed enforcement** — procedures often carry stricter
  limits than the ICAO 250 kt / FL100 rule (e.g. 220 kt on a turn, 180 kt
  inside terminal area, 180 kt at IAF). Effective speed restriction at any
  moment must be `min(250 kt, active_SID/STAR_waypoint.speed_kt)`. Message
  text carries the actual N ("reduce speed, N knots or less"). Speed field
  is already extracted by the CIFP reader (`StarWaypoint.speed_kt`); the
  route tracker knows the active waypoint.

---

## Candidate items — target release undecided

- **Proactive sector handoff (5–10 NM before boundary)** — current handoff
  logic fires reactively when `ctx.enclosing_airspaces` detects the aircraft
  has already crossed into the new sector. Real ATC hands off *before* the
  boundary so the pilot is checked in with the new controller by the time
  they cross. Needs boundary-distance calculation from `openair_db` polygon
  edges. May land in v4.3.0 or later.

---

## Supporting Infrastructure

| Item | Status |
|------|--------|
| Transcript log (`<plugin>/Resources/transcript.log`) | done |
| Cross-track error monitoring (SimBrief navlog) | done (Phase 3) |
| Altitude deviation warning (unit-consistent FL vs feet) | done (v4.2.1) |
| CIFP reader (SID / STAR / approach / FAF / procedure waypoints) | done |
| Route tracker (`s_route_fix_idx`, IAF/FAF guards) | done (v4.2.1) |
| `earth_fix.dat` / `earth_nav.dat` waypoint resolution | not started |
| Taxi routing — Dijkstra on apt.dat 1201/1202 graph | not started |
| Navigraph data support | not started |
| End-to-end IFR scenario test (`test_ifr_lflp_lfmn.cpp`) | not started |

---

## STT improvements

| Item | Status |
|------|--------|
| Voxtral `context_bias` (airport / navlog / STAR / callsign / mishearings) | shipped v4.1.4 |
| `initial_prompt` enrichment with IFR vocabulary | shipped v4.1.4 |
| Dynamic runway bias ("R-NAV NN / RNAV NN" for the assigned approach runway) | shipped v4.2.1 |
| Callsign last-two-letter bigram in STT prompt ("Romeo Charlie") | shipped v4.2.1 |
| `whisper-small-atco2-asr` GGUF conversion | pending |
| `whisper-large-v3-atco2` (GPU required) | pending |
| WhisperATC (`ggml-base.en-atc.bin`) selectable in Settings | **hidden / disabled** — the code path exists but the model garbles ICAO idents worse than Voxtral in testing; not exposed to users |

## Readback verifier

- FL / altitude / feet+QNH / frequency / squawk / runway **are** verified —
  see `src/atc/readback_verifier.cpp`. Mismatch produces
  "negative, [expected], readback".
- Fix / waypoint / SID / STAR **identifier** readback is not verified —
  Voxtral garbles multi-letter idents too often for a strict compare to be
  safe (ROMAM → "Rome, Am"; BISBO → "BISBOARD"). Deferred until a fuzzy-
  match approach (NATO tokenisation + Levenshtein tolerance) is added.
- **WhisperATC is NOT the path forward** — fine-tuned aviation Whisper models
  were evaluated; comprehension of general ICAO phraseology is worse than
  Voxtral.

---

## Wishlist

- Airport data source indicator in UI (Global apt.dat / Custom Scenery / Navigraph)
  — helps diagnose false ATIS frequencies (e.g. AFIS coded as row-50 ATIS)
- IFR holding patterns: "hold at XAMUR, inbound 270, right turns, EFC 1430"
- En-route traffic separation: RVSM 1000 ft / 5 NM conflict resolution
- Per-airport departure/arrival rules per RWY+SID (config-driven system for
  special altitude constraints — LFLP RWY 04 ROMA2A ceiling/vis minimum
  currently hard-coded)
- Visual SID minimums (ceiling from METAR — data already in `XPlaneContext`)
- Probabilistic direct-to shortcut (20% chance)
- Independent STT / LM / TTS backend selection (mix per stage)
