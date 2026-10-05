#include <xrpl/beast/utility/Journal.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace beast {
namespace {

// Counts the messages that actually reach the sink, so the tests can assert on
// which severities the journal let through.
class CountingSink : public Journal::Sink
{
    std::size_t count_{0};

public:
    CountingSink() : Sink(Severity::Warning, false)
    {
    }

    [[nodiscard]] std::size_t
    count() const
    {
        return count_;
    }

    void
    write(Severity level, std::string const&) override
    {
        if (level >= threshold())
            ++count_;
    }

    void
    writeAlways(Severity, std::string const&) override
    {
        ++count_;
    }
};

}  // namespace

TEST(Journal, info_threshold)
{
    CountingSink sink;
    sink.threshold(Severity::Info);
    Journal const j(sink);

    j.trace() << " ";
    EXPECT_EQ(sink.count(), 0u);
    j.debug() << " ";
    EXPECT_EQ(sink.count(), 0u);
    j.info() << " ";
    EXPECT_EQ(sink.count(), 1u);
    j.warn() << " ";
    EXPECT_EQ(sink.count(), 2u);
    j.error() << " ";
    EXPECT_EQ(sink.count(), 3u);
    j.fatal() << " ";
    EXPECT_EQ(sink.count(), 4u);
}

TEST(Journal, debug_threshold)
{
    CountingSink sink;
    sink.threshold(Severity::Debug);
    Journal const j(sink);

    j.trace() << " ";
    EXPECT_EQ(sink.count(), 0u);
    j.debug() << " ";
    EXPECT_EQ(sink.count(), 1u);
    j.info() << " ";
    EXPECT_EQ(sink.count(), 2u);
    j.warn() << " ";
    EXPECT_EQ(sink.count(), 3u);
    j.error() << " ";
    EXPECT_EQ(sink.count(), 4u);
    j.fatal() << " ";
    EXPECT_EQ(sink.count(), 5u);
}

// A Journal holds a reference to its sink rather than a copy of the threshold,
// so lowering the threshold has to take effect on a Journal that already exists.
// The two tests above each build the Journal after setting the threshold, so
// neither of them would notice if that stopped working.
TEST(Journal, threshold_change_applies_to_an_existing_journal)
{
    CountingSink sink;
    sink.threshold(Severity::Info);
    Journal const j(sink);

    j.debug() << " ";
    EXPECT_EQ(sink.count(), 0u);

    sink.threshold(Severity::Debug);

    j.debug() << " ";
    EXPECT_EQ(sink.count(), 1u);
}

}  // namespace beast
