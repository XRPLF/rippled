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

#ifndef BEAST_CORE_SYSTEM_BOOSTPLACEHOLDERSFIX_H_INCLUDED
#define BEAST_CORE_SYSTEM_BOOSTPLACEHOLDERSFIX_H_INCLUDED

#if BEAST_USE_BOOST_FEATURES

// Prevent <boost/bind/placeholders.hpp> from being included
#ifdef BOOST_BIND_PLACEHOLDERS_HPP_INCLUDED
# error "boost/bind.hpp must not be included before this file"
#else
# define BOOST_BIND_PLACEHOLDERS_HPP_INCLUDED
#endif

#include <boost/version.hpp>
#include <boost/bind.hpp>
#include <boost/bind/arg.hpp>

// This is a hack to fix boost's goofy placeholders going into the global
// namespace. First we prevent the user from including boost/bind.hpp
// before us. Then we define the include guard macro and include
// boost/bind.hpp ourselves to get the declarations. Finally we repeat
// the missing placeholder declarations but put them in a proper namespace.
//
// We put the placeholders in boost::placeholders so they can be accessed
// explicitly to handle the common case of a "using namespace oost" directive
// being in effect.
//
// Declarations based on boost/bind/placeholders.cpp
//
namespace boost {
namespace placeholders {
extern boost::arg<1> _1;
extern boost::arg<2> _2;
extern boost::arg<3> _3;
extern boost::arg<4> _4;
extern boost::arg<5> _5;
extern boost::arg<6> _6;
extern boost::arg<7> _7;
extern boost::arg<8> _8;
extern boost::arg<9> _9;
}
using namespace placeholders;
}

#endif

#endif
