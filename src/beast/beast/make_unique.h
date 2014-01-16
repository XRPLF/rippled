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

#ifndef BEAST_MAKE_UNIQUE_H_INCLUDED
#define BEAST_MAKE_UNIQUE_H_INCLUDED

#include <boost/config.hpp>

namespace std {

#ifdef BOOST_NO_CXX11_VARIADIC_TEMPLATES

template <class T>
std::unique_ptr <T> make_unique ()
{
    return std::unique_ptr <T> (new T);
}

template <class T, class P1>
std::unique_ptr <T> make_unique (P1&& p1)
{
    return std::unique_ptr <T> (new T (std::forward <P1> (p1)));
}

template <class T, class P1, class P2>
std::unique_ptr <T> make_unique (P1&& p1, P2&& p2)
{
    return std::unique_ptr <T> (new T (
        std::forward <P1> (p1), std::forward <P2> (p2)));
}

template <class T, class P1, class P2, class P3>
std::unique_ptr <T> make_unique (P1&& p1, P2&& p2, P3&& p3)
{
    return std::unique_ptr <T> (new T (
        std::forward <P1> (p1), std::forward <P2> (p2), std::forward <P3> (p3)));
}

template <class T, class P1, class P2, class P3, class P4>
std::unique_ptr <T> make_unique (P1&& p1, P2&& p2, P3&& p3, P4&& p4)
{
    return std::unique_ptr <T> (new T (
        std::forward <P1> (p1), std::forward <P2> (p2), std::forward <P3> (p3), 
        std::forward <P4> (p4)));
}

template <class T, class P1, class P2, class P3, class P4, class P5>
std::unique_ptr <T> make_unique (P1&& p1, P2&& p2, P3&& p3, P4&& p4, P5&& p5)
{
    return std::unique_ptr <T> (new T (
        std::forward <P1> (p1), std::forward <P2> (p2), std::forward <P3> (p3), 
        std::forward <P4> (p4), std::forward <P5> (p5)));
}

template <class T, class P1, class P2, class P3, class P4, class P5, class P6>
std::unique_ptr <T> make_unique (P1&& p1, P2&& p2, P3&& p3, P4&& p4, P5&& p5, P6&& p6)
{
    return std::unique_ptr <T> (new T (
        std::forward <P1> (p1), std::forward <P2> (p2), std::forward <P3> (p3), 
        std::forward <P4> (p4), std::forward <P5> (p5), std::forward <P6> (p6)));
}

//------------------------------------------------------------------------------

#else

template <class T, class... Args>
std::unique_ptr <T> make_unique (Args&&... args)
{
    return std::unique_ptr <T> (new T (std::forward <Args> (args)...));
}

#endif

}

#endif
