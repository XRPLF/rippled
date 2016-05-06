//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_UNIT_TEST_BASIC_STD_OSTREAM_HPP
#define BEAST_UNIT_TEST_BASIC_STD_OSTREAM_HPP

#include <beast/unit_test/basic_abstract_ostream.hpp>
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

using std_ostream = basic_std_ostream <char>;

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

} // beast

#endif
