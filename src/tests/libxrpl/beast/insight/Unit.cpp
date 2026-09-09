/**
 * GTest unit tests for beast::insight::Unit and its plumbing.
 *
 * A metric's unit decides two things that are invisible at the call site: the
 * name suffix the exporter appends, and which bucket ladder the histogram
 * view applies. Getting it wrong is silent -- a byte count declared as
 * milliseconds still records, still exports, still draws a graph, and the
 * graph is wrong. So each hop the unit has to survive is asserted here
 * rather than left to inspection.
 *
 * The hop that matters most is the group wrapper. Call sites reach a
 * collector through Groups, so a unit that reaches OTelCollector correctly
 * but is dropped by the group prefixing layer would pass a naive test while
 * failing in production.
 */

#include <xrpl/beast/insight/Unit.h>

#include <xrpl/beast/insight/Event.h>
#include <xrpl/beast/insight/EventImpl.h>
#include <xrpl/beast/insight/Groups.h>
#include <xrpl/beast/insight/NullCollector.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace beast::insight {

namespace {

/**
 * An EventImpl that records what it was notified with.
 *
 * Needed because every shipped implementation either discards the sample
 * (NullCollector) or sends it somewhere external. Asserting the recorded
 * value proves the raw-integral path preserves it, rather than only proving
 * that notify() can be called without crashing.
 */
class RecordingEventImpl : public EventImpl
{
public:
    explicit RecordingEventImpl(Unit unit) : EventImpl(unit)
    {
    }

    void
    notify(value_type const& value) override
    {
        samples.push_back(value);
    }

    /**
     * Every value passed to notify(), in call order.
     */
    std::vector<value_type> samples;
};

}  // namespace

// The unit code is a contract with the collector's Prometheus exporter: it
// derives the exported name suffix from this string. Assert the exact codes,
// not merely that they differ.
TEST(InsightUnit, otelCodeIsTheUcumCodeForEachUnit)
{
    EXPECT_STREQ(otelUnitCode(Unit::Millis), "ms");
    EXPECT_STREQ(otelUnitCode(Unit::Bytes), "By");
}

// The description is what an operator reads in the metric catalogue, so a
// byte-valued instrument must not describe itself as a duration.
TEST(InsightUnit, descriptionMatchesWhatTheUnitActuallyMeasures)
{
    EXPECT_STREQ(otelUnitDescription(Unit::Millis), "Duration in ms");
    EXPECT_STREQ(otelUnitDescription(Unit::Bytes), "Size in bytes");
}

TEST(InsightUnit, defaultEventUnitIsMillisForBackwardCompatibility)
{
    // Every pre-existing makeEvent(name) call site records a duration, so the
    // one-argument overload must keep meaning milliseconds.
    auto const collector = NullCollector::make();
    auto const event = collector->makeEvent("legacy");
    ASSERT_NE(event.impl(), nullptr);
    EXPECT_EQ(event.impl()->unit(), Unit::Millis);
}

TEST(InsightUnit, makeEventCarriesTheRequestedUnitToTheImpl)
{
    auto const collector = NullCollector::make();
    auto const event = collector->makeEvent("size", Unit::Bytes);
    ASSERT_NE(event.impl(), nullptr);
    EXPECT_EQ(event.impl()->unit(), Unit::Bytes);
}

TEST(InsightUnit, prefixedMakeEventCarriesTheUnit)
{
    auto const collector = NullCollector::make();
    auto const event = collector->makeEvent("rpc", "size", Unit::Bytes);
    ASSERT_NE(event.impl(), nullptr);
    EXPECT_EQ(event.impl()->unit(), Unit::Bytes);
}

TEST(InsightUnit, groupWrapperForwardsTheUnitAlongWithThePrefix)
{
    // ServerHandler creates its events through a Group, not through the
    // collector directly. If the group's makeEvent override forwards only the
    // name, the unit silently reverts to milliseconds and the byte histogram
    // inherits the latency ladder again.
    auto const collector = NullCollector::make();
    auto const groups = makeGroups(collector);
    auto const event = groups->get("rpc")->makeEvent("size", Unit::Bytes);
    ASSERT_NE(event.impl(), nullptr);
    EXPECT_EQ(event.impl()->unit(), Unit::Bytes);
}

TEST(InsightUnit, groupWrapperStillDefaultsToMillis)
{
    auto const collector = NullCollector::make();
    auto const groups = makeGroups(collector);
    auto const event = groups->get("rpc")->makeEvent("time");
    ASSERT_NE(event.impl(), nullptr);
    EXPECT_EQ(event.impl()->unit(), Unit::Millis);
}

TEST(InsightUnit, rawIntegralNotifyPreservesTheValueExactly)
{
    // The byte path must not be rounded or scaled on its way through the
    // duration-typed storage field.
    auto const impl = std::make_shared<RecordingEventImpl>(Unit::Bytes);
    Event const event(impl);

    event.notify(std::uint64_t{4096});
    event.notify(std::uint64_t{0});
    event.notify(std::uint64_t{1'048'577});

    ASSERT_EQ(impl->samples.size(), 3U);
    EXPECT_EQ(impl->samples[0].count(), 4096);
    EXPECT_EQ(impl->samples[1].count(), 0);
    EXPECT_EQ(impl->samples[2].count(), 1'048'577);
}

TEST(InsightUnit, durationNotifyStillRoundsUpToWholeMilliseconds)
{
    // Pre-existing behaviour, asserted so the new overload cannot quietly
    // change it: Event applies ceil to whole milliseconds, which is why
    // sub-millisecond resolution is impossible on this path.
    auto const impl = std::make_shared<RecordingEventImpl>(Unit::Millis);
    Event const event(impl);

    event.notify(std::chrono::microseconds{40});
    event.notify(std::chrono::microseconds{1'000});
    event.notify(std::chrono::milliseconds{7});

    ASSERT_EQ(impl->samples.size(), 3U);
    EXPECT_EQ(impl->samples[0].count(), 1) << "40us must round up to 1ms, not down to 0";
    EXPECT_EQ(impl->samples[1].count(), 1);
    EXPECT_EQ(impl->samples[2].count(), 7);
}

TEST(InsightUnit, notifyOnANullEventIsSafeForBothOverloads)
{
    // A default-constructed Event has no impl. Both overloads must be no-ops
    // rather than dereferencing null.
    Event const none;
    ASSERT_EQ(none.impl(), nullptr);
    EXPECT_NO_THROW(none.notify(std::uint64_t{4096}));
    EXPECT_NO_THROW(none.notify(std::chrono::milliseconds{5}));
}

}  // namespace beast::insight
