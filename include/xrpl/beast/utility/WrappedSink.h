//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_UTILITY_WRAPPEDSINK_H_INCLUDED
#define BEAST_UTILITY_WRAPPEDSINK_H_INCLUDED

#include <xrpl/beast/utility/Journal.h>

namespace beast {

/** Wraps a Journal::Sink to prefix its output with a string. */

// A WrappedSink both is a Sink and has a Sink:
//   o It inherits from Sink so it has the correct interface.
//   o It has a sink (reference) so it preserves the passed write() behavior.
// The data inherited from the base class is ignored.
class WrappedSink : public beast::Journal::Sink
{
private:
    beast::Journal::Sink& sink_;
    std::string prefix_;

public:
    explicit WrappedSink(
        beast::Journal::Sink& sink,
        std::string const& prefix = "")
        : Sink(sink), sink_(sink), prefix_(prefix)
    {
    }

    explicit WrappedSink(
        beast::Journal const& journal,
        std::string const& prefix = "")
        : WrappedSink(journal.sink(), prefix)
    {
    }

    void
    prefix(std::string const& s)
    {
        prefix_ = s;
    }

    bool
    active(beast::severities::Severity level) const override
    {
        return sink_.active(level);
    }

    bool
    console() const override
    {
        return sink_.console();
    }

    void
    console(bool output) override
    {
        sink_.console(output);
    }

    beast::severities::Severity
    threshold() const override
    {
        return sink_.threshold();
    }

    void
    threshold(beast::severities::Severity thresh) override
    {
        sink_.threshold(thresh);
    }

    void
    write(beast::severities::Severity level, std::string const& text) override
    {
        using beast::Journal;
        sink_.write(level, prefix_ + text);
    }

    void
    writeAlways(severities::Severity level, std::string const& text) override
    {
        using beast::Journal;
        sink_.writeAlways(level, prefix_ + text);
    }
};

}  // namespace beast

#endif
