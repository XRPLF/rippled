#include <xrpl/beast/utility/Journal.h>

#include <gtest/gtest.h>

#include <string>

namespace beast {
namespace {

// Counts the messages that actually reach the sink, so the tests can assert on
// which severities the journal let through.
class CountingSink : public Journal::Sink
{
public:
    CountingSink() : Sink(Severity::Warning, false)
    {
    }

    [[nodiscard]] int
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

private:
    int count_{0};
};

}  // namespace

TEST(Journal, infoThreshold)
{
    CountingSink sink;
    sink.threshold(Severity::Info);
    Journal const j(sink);

    j.trace() << " ";
    EXPECT_EQ(sink.count(), 0);
    j.debug() << " ";
    EXPECT_EQ(sink.count(), 0);
    j.info() << " ";
    EXPECT_EQ(sink.count(), 1);
    j.warn() << " ";
    EXPECT_EQ(sink.count(), 2);
    j.error() << " ";
    EXPECT_EQ(sink.count(), 3);
    j.fatal() << " ";
    EXPECT_EQ(sink.count(), 4);
}

TEST(Journal, debugThreshold)
{
    CountingSink sink;
    sink.threshold(Severity::Debug);
    Journal const j(sink);

    j.trace() << " ";
    EXPECT_EQ(sink.count(), 0);
    j.debug() << " ";
    EXPECT_EQ(sink.count(), 1);
    j.info() << " ";
    EXPECT_EQ(sink.count(), 2);
    j.warn() << " ";
    EXPECT_EQ(sink.count(), 3);
    j.error() << " ";
    EXPECT_EQ(sink.count(), 4);
    j.fatal() << " ";
    EXPECT_EQ(sink.count(), 5);
}

}  // namespace beast
