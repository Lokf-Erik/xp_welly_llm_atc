// Catch2 unit tests for simbrief_ofp::parse_route_steps.
//
// Validates the ICAO-route step-marker extractor that drives
// poll_enroute sub-phase 1.7 when the FPL includes explicit
// "<FIX>/N<spd>F<FL>" step points. The parser is authoritative for what
// enroute ATC clearances get emitted, so every route shape the plugin
// might see needs a pinned expectation here.

#include "data/simbrief_ofp.hpp"

#include <catch2/catch_amalgamated.hpp>

TEST_CASE("parse_route_steps: LIMF -> LFLP filed route", "[simbrief][fpl]") {
    // The exact SimBrief route observed for the LIMF->LFLP OFP
    // (2026-07-08 retest). One filed FL step at BANKO.
    const std::string route =
        "DCT KUKEV L50 BANKO/N0307F210 Y52 SALEV DCT";

    auto steps = simbrief_ofp::parse_route_steps(route);
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].ident == "BANKO");
    CHECK(steps[0].cruise_fl == 210);
}

TEST_CASE("parse_route_steps: two filed steps (BANKO + SALEV)",
          "[simbrief][fpl]") {
    // Variant with both step markers filed. Order matters — the parser
    // must preserve filed order so the walker fires them one by one.
    const std::string route =
        "KUKEV L50 BANKO/N0307F210 Y52 SALEV/N0378F210 SALEV3P LFLP";

    auto steps = simbrief_ofp::parse_route_steps(route);
    REQUIRE(steps.size() == 2);
    CHECK(steps[0].ident == "BANKO");
    CHECK(steps[0].cruise_fl == 210);
    CHECK(steps[1].ident == "SALEV");
    CHECK(steps[1].cruise_fl == 210);
}

TEST_CASE("parse_route_steps: no step markers (single cruise FL)",
          "[simbrief][fpl]") {
    // LFLP->LFQA style: short flight, one cruise FL, no in-route steps.
    // Parser must return empty so poll_enroute falls back to the
    // navlog-driven walker.
    const std::string route = "BULOL DCT LFQA";
    auto steps = simbrief_ofp::parse_route_steps(route);
    CHECK(steps.empty());
}

TEST_CASE("parse_route_steps: empty and malformed inputs",
          "[simbrief][fpl]") {
    CHECK(simbrief_ofp::parse_route_steps("").empty());
    CHECK(simbrief_ofp::parse_route_steps("DCT").empty());
    // "/N0300F210" alone (no ident before slash) must not parse.
    CHECK(simbrief_ofp::parse_route_steps("/N0300F210").empty());
    // Ident too long (SID-like designator).
    CHECK(simbrief_ofp::parse_route_steps("SALEV3P/N0300F210").empty());
    // Missing FL portion.
    CHECK(simbrief_ofp::parse_route_steps("BANKO/N0307").empty());
    // Non-numeric FL.
    CHECK(simbrief_ofp::parse_route_steps("BANKO/N0307FXY0").empty());
    // Two-digit FL (real filed FLs are always 3 digits, "010"-"600").
    CHECK(simbrief_ofp::parse_route_steps("BANKO/N0307F21").empty());
}

TEST_CASE("parse_route_steps: Mach-format step (M0.78)", "[simbrief][fpl]") {
    // High-altitude jets file Mach speed with the M-form instead of N.
    // Only the F<FL> portion matters — parser must accept M-form too.
    const std::string route = "GONUP UN871 BANKO/M082F350 DCT";
    auto steps = simbrief_ofp::parse_route_steps(route);
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].ident == "BANKO");
    CHECK(steps[0].cruise_fl == 350);
}

TEST_CASE("parse_route_steps: ignores intermediate airway fixes",
          "[simbrief][fpl]") {
    // Airway tokens (L50, Y52, UN871) must not be parsed as step markers.
    // They contain no '/'. The route below deliberately mixes airways
    // with two step-marker fixes to confirm the airway tokens are silent.
    const std::string route =
        "KUKEV L50 GOLEB Y52 BANKO/N0300F220 UN871 SALEV/N0280F180 DCT";
    auto steps = simbrief_ofp::parse_route_steps(route);
    REQUIRE(steps.size() == 2);
    CHECK(steps[0].ident == "BANKO");
    CHECK(steps[0].cruise_fl == 220);
    CHECK(steps[1].ident == "SALEV");
    CHECK(steps[1].cruise_fl == 180);
}

TEST_CASE("parse_route_steps: atc.route form with leading N0311F220 prefix",
          "[simbrief][fpl]") {
    // atc.route as SimBrief actually serves it for LIMF->LFLP: the leading
    // "N0311F220" is the initial-speed/level marker (no slash, no ident
    // prefix). The parser must ignore it and extract only BANKO/N0307F210.
    const std::string route =
        "N0311F220 DCT KUKEV L50 BANKO/N0307F210 Y52 SALEV DCT";
    auto steps = simbrief_ofp::parse_route_steps(route);
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].ident == "BANKO");
    CHECK(steps[0].cruise_fl == 210);
}
