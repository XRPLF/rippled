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

#ifndef BEAST_STREAMS_BASIC_STD_OSTREAM_H_INCLUDED
#define BEAST_STREAMS_BASIC_STD_OSTREAM_H_INCLUDED

#include <beast/streams/basic_abstract_ostream.h>

#include <ostream>

namespace beast {

/** Wraps an existing std::basic_ostream as an abstract_ostream. */
template <
    class CharT,
    class Traits = std::char_traits <CharT>
>
class basic_std_ostream
    : public basic_abstract_ostream <CharT, Traits>
{
private:
    using typename basic_abstract_ostream <CharT, Traits>::string_type;

    std::reference_wrapper <std::ostream> m_stream;

public:
    explicit basic_std_ostream (
        std::basic_ostream <CharT, Traits>& stream)
        : m_stream (stream)
    {
    }

    void
    write (string_type const& s) override
    {
        m_stream.get() << s << std::endl;
    }
};

typedef basic_std_ostream <char> std_ostream;

//------------------------------------------------------------------------------

/** Returns a basic_std_ostream using template argument deduction. */
template <
    class CharT,
    class Traits = std::char_traits <CharT>
>
basic_std_ostream <CharT, Traits>
make_std_ostream (std::basic_ostream <CharT, Traits>& stream)
{
    return basic_std_ostream <CharT, Traits> (stream);
}

}

#endif
