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

#ifndef BEAST_MPL_POINTERTOOTHERH_INCLUDED
#define BEAST_MPL_POINTERTOOTHERH_INCLUDED

namespace beast {
namespace mpl {

// Ideas based on boost

/** Declares a type which is a pointer or smart pointer to U, depending on T.
    This works for smart pointer containers with up to three template
    parameters. More specializations can be added for containers with
    more than three template parameters.
*/
/** @{ */
template <class T, class U>
struct PointerToOther;

template <class T, class U,
    template <class> class SmartPointer>
struct PointerToOther <SmartPointer <T>, U>
{
   typedef SmartPointer <U> type;
};

template <class T, class T2, class U,
    template <class, class> class SmartPointer>
struct PointerToOther <SmartPointer <T, T2>, U>
{
   typedef SmartPointer <U, T2> type;
};

template <class T, class T2, class T3, class U,
        template<class, class, class> class SmartPointer>
struct PointerToOther <SmartPointer <T, T2, T3>, U>
{
   typedef SmartPointer <U, T2, T3> type;
};

template <class T, class U>
struct PointerToOther <T*, U>
{
   typedef U* type;
};
/** @} */

}
}

#endif
