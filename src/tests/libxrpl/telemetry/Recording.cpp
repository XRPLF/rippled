/**
 * Tests for the telemetry recording utilities.
 *
 * Compiled in every build. Each test asserts the compiled-in behaviour when
 * kEnabled and the compiled-out behaviour otherwise, so both configurations
 * are pinned by the same file rather than one of them going unasserted.
 */

#include <xrpl/telemetry/Recording.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <thread>
#include <type_traits>

using namespace xrpl;

// kEnabled must agree with the macro that drives every branch below. Asserted
// at compile time so a mismatch cannot reach the runtime assertions.
#ifdef XRPL_ENABLE_TELEMETRY
static_assert(telemetry::kEnabled, "kEnabled must be true when the macro is defined");
#else
static_assert(!telemetry::kEnabled, "kEnabled must be false when the macro is absent");
#endif

// The whole point of the compiled-out form is that it costs no storage. An
// empty type contributes nothing as a [[no_unique_address]] member.
TEST(Recording, compiled_out_types_are_empty)
{
    if constexpr (telemetry::kEnabled)
    {
        EXPECT_FALSE(std::is_empty_v<telemetry::Counter<>>);
        EXPECT_FALSE(std::is_empty_v<telemetry::Stopwatch>);
    }
    else
    {
        EXPECT_TRUE(std::is_empty_v<telemetry::Counter<>>);
        EXPECT_TRUE(std::is_empty_v<telemetry::Stopwatch>);
    }
}

// A fresh counter reads zero in both configurations -- the one value that must
// agree, since callers may report it unconditionally.
TEST(Recording, counter_starts_at_zero)
{
    telemetry::Counter<> counter;
    EXPECT_EQ(counter.load(), 0U);
}

// add() accumulates exactly when compiled in, and stays at zero when not.
TEST(Recording, counter_accumulates_only_when_compiled_in)
{
    telemetry::Counter<> counter;
    counter.add();
    counter.add(4);

    if constexpr (telemetry::kEnabled)
        EXPECT_EQ(counter.load(), 5U);
    else
        EXPECT_EQ(counter.load(), 0U);
}

// The default template argument is std::uint64_t; an explicit type is honoured.
TEST(Recording, counter_honours_its_value_type)
{
    static_assert(std::is_same_v<decltype(telemetry::Counter<>{}.load()), std::uint64_t>);
    static_assert(
        std::is_same_v<decltype(telemetry::Counter<std::uint32_t>{}.load()), std::uint32_t>);

    telemetry::Counter<std::uint32_t> counter;
    counter.add(7);
    EXPECT_EQ(counter.load(), telemetry::kEnabled ? 7U : 0U);
}

// A stopwatch measures a real interval when compiled in and reports exactly
// zero when not, so callers can report elapsedUs() unconditionally.
TEST(Recording, stopwatch_measures_only_when_compiled_in)
{
    telemetry::Stopwatch const timer;
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
    auto const elapsed = timer.elapsedUs();

    if constexpr (telemetry::kEnabled)
        EXPECT_GE(elapsed, std::chrono::microseconds{1000});
    else
        EXPECT_EQ(elapsed, std::chrono::microseconds{0});
}

// restart() moves the origin forward, so the interval measured after it is
// shorter than the one before it.
TEST(Recording, stopwatch_restart_resets_the_origin)
{
    telemetry::Stopwatch timer;
    std::this_thread::sleep_for(std::chrono::milliseconds{4});
    auto const beforeRestart = timer.elapsedUs();

    timer.restart();
    auto const afterRestart = timer.elapsedUs();

    if constexpr (telemetry::kEnabled)
    {
        EXPECT_GE(beforeRestart, std::chrono::microseconds{2000});
        EXPECT_LT(afterRestart, beforeRestart);
    }
    else
    {
        EXPECT_EQ(beforeRestart, std::chrono::microseconds{0});
        EXPECT_EQ(afterRestart, std::chrono::microseconds{0});
    }
}
