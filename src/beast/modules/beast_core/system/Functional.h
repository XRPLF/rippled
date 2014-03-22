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

#ifndef BEAST_FUNCTIONAL_H_INCLUDED
#define BEAST_FUNCTIONAL_H_INCLUDED

namespace beast
{

//------------------------------------------------------------------------------

/*  Brings functional support into our namespace, based on environment.

    Notes on bind

    Difference between boost::bind and std::bind
    http://stackoverflow.com/questions/10555566/is-there-any-difference-between-c11-stdbind-and-boostbind

    Resolving conflict between boost::shared_ptr and std::shared_ptr
    http://stackoverflow.com/questions/4682343/how-to-resolve-conflict-between-boostshared-ptr-and-using-stdshared-ptr
*/ 

#ifndef BEAST_BIND_PLACEHOLDERS_N
# if BEAST_MSVC && BEAST_FUNCTIONAL_USES_STD
#  define BEAST_BIND_PLACEHOLDERS_N 20 // Visual Studio 2012
# else
#  define BEAST_BIND_PLACEHOLDERS_N 8 // Seems a reasonable number
# endif
#endif

/** Max number of arguments to bind, total.
*/
#if BEAST_MSVC
# ifdef _VARIADIC_MAX
#  define BEAST_VARIADIC_MAX _VARIADIC_MAX
# else
#  define BEAST_VARIADIC_MAX 10
# endif
#else
# define BEAST_VARIADIC_MAX 10
#endif

//------------------------------------------------------------------------------

#if BEAST_FUNCTIONAL_USES_STD

namespace functional
{

using std::ref;
using std::cref;
using std::bind;

//using std::function;

}

using namespace functional;

namespace placeholders
{

#if BEAST_BIND_PLACEHOLDERS_N >= 1
using std::placeholders::_1;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 2
using std::placeholders::_2;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 3
using std::placeholders::_3;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 4
using std::placeholders::_4;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 5
using std::placeholders::_5;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 6
using std::placeholders::_6;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 7
using std::placeholders::_7;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 8
using std::placeholders::_8;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 9
using std::placeholders::_9;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 10
using std::placeholders::_10;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 11
using std::placeholders::_11;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 12
using std::placeholders::_12;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 13
using std::placeholders::_13;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 14
using std::placeholders::_14;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 15
using std::placeholders::_15;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 16
using std::placeholders::_16;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 17
using std::placeholders::_17;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 18
using std::placeholders::_18;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 19
using std::placeholders::_19;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 20
using std::placeholders::_20;
#endif

}

using namespace placeholders;

//------------------------------------------------------------------------------

#elif BEAST_FUNCTIONAL_USES_TR1

namespace functional
{

using std::tr1::ref;
using std::tr1::cref;
using std::tr1::bind;

//using std::tr1::function;

}

using namespace functional;

namespace placeholders
{

#if BEAST_BIND_PLACEHOLDERS_N >= 1
using std::tr1::placeholders::_1;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 2
using std::tr1::placeholders::_2;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 3
using std::tr1::placeholders::_3;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 4
using std::tr1::placeholders::_4;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 5
using std::tr1::placeholders::_5;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 6
using std::tr1::placeholders::_6;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 7
using std::tr1::placeholders::_7;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 8
using std::tr1::placeholders::_8;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 9
using std::tr1::placeholders::_9;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 10
using std::tr1::placeholders::_10;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 11
using std::tr1::placeholders::_11;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 12
using std::tr1::placeholders::_12;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 13
using std::tr1::placeholders::_13;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 14
using std::tr1::placeholders::_14;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 15
using std::tr1::placeholders::_15;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 16
using std::tr1::placeholders::_16;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 17
using std::tr1::placeholders::_17;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 18
using std::tr1::placeholders::_18;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 19
using std::tr1::placeholders::_19;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 20
using std::tr1::placeholders::_20;
#endif

}

using namespace placeholders;

//------------------------------------------------------------------------------

#elif BEAST_FUNCTIONAL_USES_BOOST

namespace functional
{

using boost::ref;
using boost::cref;
using boost::bind;

//using boost::function;

}

using namespace functional;

namespace placeholders
{

#if BEAST_BIND_PLACEHOLDERS_N >= 1
using boost::placeholders::_1;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 2
using boost::placeholders::_2;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 3
using boost::placeholders::_3;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 4
using boost::placeholders::_4;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 5
using boost::placeholders::_5;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 6
using boost::placeholders::_6;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 7
using boost::placeholders::_7;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 8
using boost::placeholders::_8;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 9
using boost::placeholders::_9;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 10
using boost::placeholders::_10;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 11
using boost::placeholders::_11;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 12
using boost::placeholders::_12;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 13
using boost::placeholders::_13;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 14
using boost::placeholders::_14;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 15
using boost::placeholders::_15;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 16
using boost::placeholders::_16;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 17
using boost::placeholders::_17;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 18
using boost::placeholders::_18;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 19
using boost::placeholders::_19;
#endif

#if BEAST_BIND_PLACEHOLDERS_N >= 20
using boost::placeholders::_20;
#endif

}

using namespace placeholders;

//------------------------------------------------------------------------------

#else

#error Unknown bind source in Functional.h

#endif

} // beast

#endif
