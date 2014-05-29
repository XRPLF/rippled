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

#ifndef BEAST_STREAMS_BASIC_SCOPED_OSTREAM_H_INCLUDED
#define BEAST_STREAMS_BASIC_SCOPED_OSTREAM_H_INCLUDED

#include <beast/cxx14/memory.h> // <memory>

#include <functional>
#include <memory>
#include <sstream>

// gcc libstd++ doesn't have move constructors for basic_ostringstream
// http://gcc.gnu.org/bugzilla/show_bug.cgi?id=54316
//
#ifndef BEAST_NO_STDLIB_STREAM_MOVE
# ifdef __GLIBCXX__
#  define BEAST_NO_STDLIB_STREAM_MOVE 1
# else
#  define BEAST_NO_STDLIB_STREAM_MOVE 0
# endif
#endif

namespace beast {

template <
    class CharT,
    class Traits
>
class basic_abstract_ostream;

/** Scoped output stream that forwards to a functor upon destruction. */
template <
    class CharT,
    class Traits = std::char_traits <CharT>,
    class Allocator = std::allocator <CharT>
>
class basic_scoped_ostream
{
private:
    typedef std::function <void (
        std::basic_string <CharT, Traits, Allocator> const&)> handler_t;

    typedef std::basic_ostringstream <
        CharT, Traits, Allocator> stream_type;

    handler_t m_handler;

#if BEAST_NO_STDLIB_STREAM_MOVE
    std::unique_ptr <stream_type> m_ss;

    stream_type& stream()
    {
        return *m_ss;
    }

#else
    stream_type m_ss;

    stream_type& stream()
    {
        return m_ss;
    }

#endif

public:
    typedef std::basic_string <CharT, Traits> string_type;

    // Disallow copy since that would duplicate the output
    basic_scoped_ostream (basic_scoped_ostream const&) = delete;
    basic_scoped_ostream& operator= (basic_scoped_ostream const) = delete;

    template <class Handler>
    explicit basic_scoped_ostream (Handler&& handler)
        : m_handler (std::forward <Handler> (handler))
    #if BEAST_NO_STDLIB_STREAM_MOVE
        , m_ss (std::make_unique <stream_type>())
    #endif
    {
    }

    template <class T, class Handler>
    basic_scoped_ostream (T const& t, Handler&& handler)
        : m_handler (std::forward <Handler> (handler))
    #if BEAST_NO_STDLIB_STREAM_MOVE
        , m_ss (std::make_unique <stream_type>())
    #endif
    {
        stream() << t;
    }

    basic_scoped_ostream (basic_abstract_ostream <
        CharT, Traits>& ostream)
        : m_handler (
            [&](string_type const& s)
            {
                ostream.write (s);
            })
    {
    }

    basic_scoped_ostream (basic_scoped_ostream&& other)
        : m_handler (std::move (other.m_handler))
        , m_ss (std::move (other.m_ss))
    {
    }

    ~basic_scoped_ostream()
    {
        auto const& s (stream().str());
        if (! s.empty())
            m_handler (s);
    }

    basic_scoped_ostream&
    operator<< (std::ostream& manip (std::ostream&))
    {
        stream() << manip;
        return *this;
    }

    template <class T>
    basic_scoped_ostream&
    operator<< (T const& t)
    {
        stream() << t;
        return *this;
    }
};

}

#endif
