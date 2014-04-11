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

#ifndef BEAST_BOOST_GET_POINTER_H_INCLUDED
#define BEAST_BOOST_GET_POINTER_H_INCLUDED

#include <boost/get_pointer.hpp>

// Boost 1.55 incorrectly defines BOOST_NO_CXX11_SMART_PTR
// when building with clang 3.4 and earlier. This workaround
// gives beast its own overloads.

#ifdef BOOST_NO_CXX11_SMART_PTR
#include <memory>
namespace beast {
template <class T>
T* get_pointer (std::unique_ptr<T> const& p)
{
    return p.get();
}

template <class T>
T* get_pointer (std::shared_ptr<T> const& p)
{
    return p.get();
}
}
#endif

#endif
