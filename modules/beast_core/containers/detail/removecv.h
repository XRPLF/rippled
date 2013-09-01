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

#ifndef BEAST_CORE_CONTAINERS_DETAIL_REMOVECV_H_INCLUDED
#define BEAST_CORE_CONTAINERS_DETAIL_REMOVECV_H_INCLUDED

namespace detail
{

// Strip all cv qualifiers from T
template <typename T>
struct removecv
{
	typedef T type;
};

template <typename T>
struct removecv <const T>
{
	typedef typename removecv <T>::type type;
};

template <typename T>
struct removecv <volatile T>
{
	typedef typename removecv <T>::type type;
};

template <typename T>
struct removecv <const volatile T>
{
	typedef typename removecv <T>::type type;
};

template <typename T>
struct removecv <T*>
{
	typedef typename removecv <T>::type type;
};

template <typename T>
struct removecv <T&>
{
	typedef typename removecv <T>::type type;
};

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
template <typename T>
struct removecv <T&&>
{
	typedef typename removecv <T>::type type;
};
#endif

}

#endif
