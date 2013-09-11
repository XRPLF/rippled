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

#ifndef BEAST_ASIO_SYSTEM_BOOSTINCLUDES_H_INCLUDED
#define BEAST_ASIO_SYSTEM_BOOSTINCLUDES_H_INCLUDED

// Make sure we take care of fixing boost::bind oddities first.
#if !defined(BEAST_CORE_H_INCLUDED)
#error beast_core.h must be included before including this file
#endif

// These should have already been set in your project, but
// if you forgot then we will be optimistic and choose the latest.
//
#if BEAST_WIN32
# ifndef _WIN32_WINNT
#  pragma message ("Warning: _WIN32_WINNT was not set in your project")
#  define _WIN32_WINNT 0x0600
# endif
# ifndef _VARIADIC_MAX
#  define _VARIADIC_MAX 10
# endif
#endif

// Unfortunately, we use some boost detail elements
//
// https://svn.boost.org/trac/boost/ticket/9024

#include <boost/version.hpp>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/function.hpp>
#include <boost/type_traits.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>

// Continuation hooks added in 1.54.0
#ifndef BEAST_ASIO_HAS_CONTINUATION_HOOKS
# if BOOST_VERSION >= 105400
#  define BEAST_ASIO_HAS_CONTINUATION_HOOKS 1
# else
#  define BEAST_ASIO_HAS_CONTINUATION_HOOKS 0
# endif
#endif
#if BEAST_ASIO_HAS_CONTINUATION_HOOKS
# include <boost/asio/detail/handler_cont_helpers.hpp>
#endif

//------------------------------------------------------------------------------

// Configure some options based on the version of boost
#if BOOST_VERSION >= 105400
# ifndef BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
#  define BEAST_ASIO_HAS_BUFFEREDHANDSHAKE 1
# endif
#else
# ifndef BEAST_ASIO_HAS_BUFFEREDHANDSHAKE
#  define BEAST_ASIO_HAS_BUFFEREDHANDSHAKE 0
# endif
#endif

#endif
