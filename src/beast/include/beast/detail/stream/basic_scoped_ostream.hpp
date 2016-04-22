//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_STREAM_BASIC_SCOPED_OSTREAM_HPP
#define BEAST_DETAIL_STREAM_BASIC_SCOPED_OSTREAM_HPP

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
namespace detail {

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
    using handler_t = std::function <void (
        std::basic_string <CharT, Traits, Allocator> const&)>;

    using stream_type = std::basic_ostringstream <
        CharT, Traits, Allocator>;

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
    using string_type = std::basic_string <CharT, Traits>;

    // Disallow copy since that would duplicate the output
    basic_scoped_ostream (basic_scoped_ostream const&) = delete;
    basic_scoped_ostream& operator= (basic_scoped_ostream const) = delete;

    template <class Handler>
    explicit basic_scoped_ostream (Handler&& handler)
        : m_handler (std::forward <Handler> (handler))
    #if BEAST_NO_STDLIB_STREAM_MOVE
        , m_ss (new stream_type())
    #endif
    {
    }

    template <class T, class Handler>
    basic_scoped_ostream (T const& t, Handler&& handler)
        : m_handler (std::forward <Handler> (handler))
    #if BEAST_NO_STDLIB_STREAM_MOVE
        , m_ss (new stream_type())
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

} // detail
} // beast

#endif
