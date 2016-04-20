//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_STREAM_BASIC_ABSTRACT_OSTREAM_HPP
#define BEAST_DETAIL_STREAM_BASIC_ABSTRACT_OSTREAM_HPP

#include <beast/detail/stream/basic_scoped_ostream.hpp>
#include <functional>
#include <memory>
#include <sstream>

namespace beast {
namespace detail {

/** Abstraction for an output stream similar to std::basic_ostream. */
template <
    class CharT,
    class Traits = std::char_traits <CharT>
>
class basic_abstract_ostream
{
public:
    using string_type = std::basic_string <CharT, Traits>;
    using scoped_stream_type = basic_scoped_ostream <CharT, Traits>;

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

} // detail
} // beast

#endif
