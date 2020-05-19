//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef TEST_UNIT_TEST_SUITE_JOURNAL_H
#define TEST_UNIT_TEST_SUITE_JOURNAL_H

#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/Journal.h>

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
};

inline void
SuiteJournalSink::write(
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

    // Only write the string if the level at least equals the threshold.
    if (level >= threshold())
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
