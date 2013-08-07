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

#ifndef BEAST_BOOSTINCLUDES_H_INCLUDED
#define BEAST_BOOSTINCLUDES_H_INCLUDED

// Make sure we take care of fixing boost::bind oddities first.
#if !defined(BEAST_CORE_H_INCLUDED)
#error beast_core.h must be included before including this file
#endif

#if BEAST_WIN32
# ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600
# endif
# ifndef _VARIADIC_MAX
#  define _VARIADIC_MAX 10
# endif
#endif

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/foreach.hpp>
#include <boost/type_traits.hpp>

//#include <boost/mpl/at.hpp>
//#include <boost/asio/io_service.hpp>
//#include <boost/array.hpp>
//#include <boost/bind.hpp>
//#include <boost/unordered_map.hpp>
//#include <boost/mpl/vector.hpp>

// VFALCO TODO check for version availability
#ifndef BOOST_ASIO_INITFN_RESULT_TYPE
#define BOOST_ASIO_INITFN_RESULT_TYPE(expr,val) void
#endif

#endif

