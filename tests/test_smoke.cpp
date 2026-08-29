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

TEST_CASE("readback verifier accepts natural speed acknowledgements",
          "[readback][speed]")
{
    const std::string clearance =
        "Foxtrot Five One, reduce speed, 250 knots or less.";

    SECTION("value followed by knots")
    {
        auto result = readback_verifier::check(
            clearance,
            "250 knots or less Foxtrot Five One");

        REQUIRE(result.empty());
    }

    SECTION("spoken digit sequence")
    {
        auto result = readback_verifier::check(
            clearance,
            "TWO FIVE ZERO KNOTS OR LESS FOXTROT FIVE ONE");

        REQUIRE(result.empty());
    }

    SECTION("reducing to assigned speed")
    {
        auto result = readback_verifier::check(
            clearance,
            "reducing to 250 knots Foxtrot Five One");

        REQUIRE(result.empty());
    }

    SECTION("reducing speed acknowledgement")
    {
        auto result = readback_verifier::check(
            clearance,
            "reducing speed Foxtrot Five One");

        REQUIRE(result.empty());
    }

    SECTION("incorrect speed is rejected")
    {
        auto result = readback_verifier::check(
            clearance,
            "reducing to 240 knots Foxtrot Five One");

        REQUIRE(result.size() == 1);
        REQUIRE(result.front().field == "speed");
        REQUIRE(result.front().expected == "250");
        REQUIRE(result.front().stated == "240");
    }
}
