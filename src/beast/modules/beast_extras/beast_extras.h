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

#ifndef BEAST_EXTRAS_H_INCLUDED
#define BEAST_EXTRAS_H_INCLUDED

// Adds boost-specific features to beast.

#include <boost/version.hpp>

// lockable_traits was added in 1.53.0
#ifndef BEAST_BOOST_HAS_LOCKABLES
# if BOOST_VERSION >= 105300
#  define BEAST_BOOST_HAS_LOCKABLES 1
# else
#  define BEAST_BOOST_HAS_LOCKABLES 0
# endif
#endif
#if BEAST_BOOST_HAS_LOCKABLES
# include <boost/thread/lockable_traits.hpp>
#endif

#if BEAST_BOOST_HAS_LOCKABLES
# include "traits/BoostLockableTraits.h"
#endif

#endif
