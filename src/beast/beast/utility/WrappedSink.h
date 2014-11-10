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

#ifndef BEAST_UTILITY_WRAPPED_SINK_H_INCLUDED
#define BEAST_UTILITY_WRAPPED_SINK_H_INCLUDED

#include <beast/utility/Journal.h>

namespace beast {

/** Wraps a Journal::Sink to prefix it's output with a string. */
class WrappedSink : public beast::Journal::Sink
{
private:
    std::string prefix_;
    beast::Journal::Sink& sink_;

public:
    explicit
    WrappedSink (beast::Journal::Sink& sink)
        : sink_ (sink)
    {
    }

    explicit
    WrappedSink (beast::Journal const& journal)
        : sink_ (journal.sink())
    {
    }

    void prefix (std::string const& s)
    {
        prefix_ = s;
    }

    bool
    active (beast::Journal::Severity level) const override
    {
        return sink_.active (level);
    }

    bool
    console () const override
    {
        return sink_.console ();
    }

    void console (bool output) override
    {
        sink_.console (output);
    }

    beast::Journal::Severity
    severity() const
    {
        return sink_.severity();
    }

    void severity (beast::Journal::Severity level)
    {
        sink_.severity (level);
    }

    void write (beast::Journal::Severity level, std::string const& text)
    {
        using beast::Journal;
        sink_.write (level, prefix_ + text);
    }
};

}

#endif
