#ifndef TEST_UNIT_TEST_SUITE_JOURNAL_H
#define TEST_UNIT_TEST_SUITE_JOURNAL_H

#include <xrpl/beast/unit_test.h>
#include <xrpl/beast/utility/Journal.h>

namespace ripple {
namespace test {

// A Journal::Sink intended for use with the beast unit test framework.
class SuiteJournalSink : public beast::Journal::Sink
{
    std::string partition_;
    beast::unit_test::suite& suite_;

public:
    SuiteJournalSink(
        std::string const& partition,
        beast::severities::Severity threshold,
        beast::unit_test::suite& suite)
        : Sink(threshold, false), partition_(partition + " "), suite_(suite)
    {
    }

    // For unit testing, always generate logging text.
    inline bool
    active(beast::severities::Severity level) const override
    {
        return true;
    }

    void
    write(beast::severities::Severity level, std::string const& text) override;

    void
    writeAlways(beast::severities::Severity level, std::string const& text)
        override;
};

inline void
SuiteJournalSink::write(
    beast::severities::Severity level,
    std::string const& text)
{
    // Only write the string if the level at least equals the threshold.
    if (level >= threshold())
        writeAlways(level, text);
}

inline void
SuiteJournalSink::writeAlways(
    beast::severities::Severity level,
    std::string const& text)
{
    using namespace beast::severities;

    char const* const s = [level]() {
        switch (level)
        {
            case kTrace:
                return "TRC:";
            case kDebug:
                return "DBG:";
            case kInfo:
                return "INF:";
            case kWarning:
                return "WRN:";
            case kError:
                return "ERR:";
            default:
                break;
            case kFatal:
                break;
        }
        return "FTL:";
    }();

    static std::mutex log_mutex;
    std::lock_guard lock(log_mutex);
    suite_.log << s << partition_ << text << std::endl;
}

class SuiteJournal
{
    SuiteJournalSink sink_;
    beast::Journal journal_;

public:
    SuiteJournal(
        std::string const& partition,
        beast::unit_test::suite& suite,
        beast::severities::Severity threshold = beast::severities::kFatal)
        : sink_(partition, threshold, suite), journal_(sink_)
    {
    }
    // Clang 10.0.0 and 10.0.1 disagree about formatting operator&
    // TBD Re-enable formatting when we upgrade to clang 11
    // clang-format off
    operator beast::Journal &()
    // clang-format on
    {
        return journal_;
    }
};

// this sink can be used to create a custom journal
// whose log messages will be captured to a stringstream
// that can be later inspected.
class StreamSink : public beast::Journal::Sink
{
    std::stringstream strm_;

public:
    StreamSink(
        beast::severities::Severity threshold = beast::severities::kDebug)
        : Sink(threshold, false)
    {
    }

    void
    write(beast::severities::Severity level, std::string const& text) override
    {
        if (level < threshold())
            return;
        writeAlways(level, text);
    }

    inline void
    writeAlways(beast::severities::Severity level, std::string const& text)
        override
    {
        strm_ << text << std::endl;
    }

    std::stringstream const&
    messages() const
    {
        return strm_;
    }
};

}  // namespace test
}  // namespace ripple

#endif
