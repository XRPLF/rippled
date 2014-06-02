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

#ifndef BEAST_STREAMS_BASIC_ABSTRACT_OSTREAM_H_INCLUDED
#define BEAST_STREAMS_BASIC_ABSTRACT_OSTREAM_H_INCLUDED

#include <beast/streams/basic_scoped_ostream.h>

#include <functional>
#include <memory>
#include <sstream>

namespace beast {

/** Abstraction for an output stream similar to std::basic_ostream. */
template <
    class CharT,
    class Traits = std::char_traits <CharT>
>
class basic_abstract_ostream
{
public:
    typedef std::basic_string <CharT, Traits> string_type;
    typedef basic_scoped_ostream <CharT, Traits> scoped_stream_type;

    basic_abstract_ostream() = default;

    virtual
    ~basic_abstract_ostream() = default;

    basic_abstract_ostream (basic_abstract_ostream const&) = default;
    basic_abstract_ostream& operator= (
        basic_abstract_ostream const&) = default;

    /** Returns `true` if the stream is active.
        Inactive streams do not produce output.
    */
    /** @{ */
    virtual
    bool
    active() const
    {
        return true;
    }

    explicit
    operator bool() const
    {
        return active();
    }
    /** @} */

    /** Called to output each string. */
    virtual
    void
    write (string_type const& s) = 0;

    scoped_stream_type
    operator<< (std::ostream& manip (std::ostream&))
    {
        return scoped_stream_type (manip,
            [this](string_type const& s)
            {
                this->write (s);
            });
    }

    template <class T>
    scoped_stream_type
    operator<< (T const& t)
    {
        return scoped_stream_type (t,
            [this](string_type const& s)
            {
                this->write (s);
            });
    }
};

}

#endif
