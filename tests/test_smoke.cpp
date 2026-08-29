#include <catch2/catch_amalgamated.hpp>
#include "atc/readback_verifier.hpp"

TEST_CASE("smoke test", "[smoke]")
{
    REQUIRE(1 + 1 == 2);
}

TEST_CASE("readback verifier accepts spoken squawk digits",
          "[readback][squawk]")
{
    const std::string clearance =
        "Foxtrot Foxtrot Alpha One Zero Five One, squawk 2777.";

    // Exact transcription observed in the simulator log.
    auto spoken = readback_verifier::check(
        clearance,
        "SQUAWK TWO SEVEN SEVEN SEVEN "
        "FOXTROT FOXTROT ALPHA ONE ZERO FIVE ONE");

    REQUIRE(spoken.empty());

    // Whisper may also return the digits themselves with spaces.
    auto spaced = readback_verifier::check(
        clearance,
        "squawk 2 7 7 7");

    REQUIRE(spaced.empty());

    // A genuinely incorrect code must still be rejected.
    auto incorrect = readback_verifier::check(
        clearance,
        "squawk two seven seven six");

    REQUIRE(incorrect.size() == 1);
    REQUIRE(incorrect.front().field == "squawk");
    REQUIRE(incorrect.front().expected == "2777");
    REQUIRE(incorrect.front().stated == "2776");
}
