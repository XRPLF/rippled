#pragma once

#include <xrpl/beast/unit_test.h>
#include <xrpl/beast/utility/Journal.h>

namespace xrpl::test {

// A Journal::Sink intended for use with the beast unit test framework.
class SuiteJournalSink : public beast::Journal::Sink
{
    std::string partition_;
    beast::unit_test::Suite& suite_;

public:
    SuiteJournalSink(
        std::string const& partition,
        beast::severities::Severity threshold,
        beast::unit_test::Suite& suite)
        : Sink(threshold, false), partition_(partition + " "), suite_(suite)
    {
    }

    // For unit testing, always generate logging text.
    [[nodiscard]] bool
    active(beast::severities::Severity level) const override
    {
        return true;
    }

    void
    write(beast::severities::Severity level, std::string const& text) override;

    void
    writeAlways(beast::severities::Severity level, std::string const& text) override;
};

inline void
SuiteJournalSink::write(beast::severities::Severity level, std::string const& text)
{
    // Only write the string if the level at least equals the threshold.
    if (level >= threshold())
        writeAlways(level, text);
}

inline void
SuiteJournalSink::writeAlways(beast::severities::Severity level, std::string const& text)
{
    using namespace beast::severities;

    char const* const s = [level]() {
        switch (level)
        {
            case KTrace:
                return "TRC:";
            case KDebug:
                return "DBG:";
            case KInfo:
                return "INF:";
            case KWarning:
                return "WRN:";
            case KError:
                return "ERR:";
            default:
                break;
            case KFatal:
                break;
        }
        return "FTL:";
    }();

    static std::mutex kLOG_MUTEX;
    std::scoped_lock const lock(kLOG_MUTEX);
    suite_.log << s << partition_ << text << std::endl;
}

class SuiteJournal
{
    SuiteJournalSink sink_;
    beast::Journal journal_;

public:
    SuiteJournal(
        std::string const& partition,
        beast::unit_test::Suite& suite,
        beast::severities::Severity threshold = beast::severities::KFatal)
        : sink_(partition, threshold, suite), journal_(sink_)
    {
    }
    operator beast::Journal&()
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
    StreamSink(beast::severities::Severity threshold = beast::severities::KDebug)
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

    void
    writeAlways(beast::severities::Severity level, std::string const& text) override
    {
        strm_ << text << std::endl;
    }

    std::stringstream const&
    messages() const
    {
        return strm_;
    }
};

}  // namespace xrpl::test
