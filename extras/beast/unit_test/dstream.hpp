//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_UNIT_TEST_DSTREAM_HPP
#define BEAST_UNIT_TEST_DSTREAM_HPP

#include <boost/utility/base_from_member.hpp>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

#ifdef _MSC_VER
# ifndef NOMINMAX
#  define NOMINMAX 1
# endif
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# include <windows.h>
# undef WIN32_LEAN_AND_MEAN
# undef NOMINMAX
#endif

namespace beast {
namespace unit_test {

namespace detail {

#ifdef _MSC_VER

template<class CharT, class Traits, class Allocator>
class dstream_buf
    : public std::basic_stringbuf<CharT, Traits, Allocator>
{
    bool dbg_;

    template<class T>
    void write(T const*) = delete;

    void write(char const* s)
    {
        if(dbg_)
            OutputDebugStringA(s);
        else
            std::cout << s;
    }

    void write(wchar_t const* s)
    {
        if(dbg_)
            OutputDebugStringW(s);
        else
            std::wcout << s;
    }

public:
    dstream_buf()
        : dbg_(IsDebuggerPresent() != FALSE)
    {
    }

    ~dstream_buf()
    {
        sync();
    }

    int
    sync() override
    {
        write(this->str().c_str());
        this->str("");
        return 0;
    }
};

#else

template<class CharT, class Traits, class Allocator>
class dstream_buf
    : public std::basic_stringbuf<CharT, Traits, Allocator>
{
    template<class T>
    void write(T const*) = delete;

    void write(char const* s)
    {
        std::cout << s;
    }

    void write(wchar_t const* s)
    {
        std::wcout << s;
    }

public:
    ~dstream_buf()
    {
        sync();
    }

    int
    sync() override
    {
        write(this->str().c_str());
        this->str("");
        return 0;
    }
};

#endif

} // detail

/// A std::ostream that redirects output to the debugger if attached.
template<
    class CharT,
    class Traits = std::char_traits<CharT>,
    class Allocator = std::allocator<CharT>
>
class basic_dstream
    : private boost::base_from_member<
        detail::dstream_buf<CharT, Traits, Allocator>>
    , public std::basic_ostream<CharT, Traits>
{
public:
    basic_dstream()
        : std::basic_ostream<CharT, Traits>(&this->member)
    {
    }
};

using dstream = basic_dstream<char>;
using dwstream = basic_dstream<wchar_t>;

} // test
} // beast

#endif
