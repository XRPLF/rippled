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

//  ratio_io
//
//  (C) Copyright Howard Hinnant
//  Use, modification and distribution are subject to the Boost Software License,
//  Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt).

#ifndef BEAST_CHRONO_RATIO_IO_H_INCLUDED
#define BEAST_CHRONO_RATIO_IO_H_INCLUDED

/*

    ratio_io synopsis

#include <ratio>
#include <string>

namespace std
{

template <class Ratio, class charT>
struct ratio_string
{
    static basic_string<charT> symbol();
    static basic_string<charT> prefix();
};

}  // std

*/

#include <ratio>
#include <string>
#include <sstream>

//_LIBCPP_BEGIN_NAMESPACE_STD
namespace std {

template <class _Ratio, class _CharT>
struct ratio_string
{
    static basic_string<_CharT> symbol() {return prefix();}
    static basic_string<_CharT> prefix();
};

template <class _Ratio, class _CharT>
basic_string<_CharT>
ratio_string<_Ratio, _CharT>::prefix()
{
    basic_ostringstream<_CharT> __os;
    __os << _CharT('[') << _Ratio::num << _CharT('/')
                        << _Ratio::den << _CharT(']');
    return __os.str();
}

// atto

template <>
struct ratio_string<atto, char>
{
    static string symbol() {return string(1, 'a');}
    static string prefix()  {return string("atto");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<atto, char16_t>
{
    static u16string symbol() {return u16string(1, u'a');}
    static u16string prefix()  {return u16string(u"atto");}
};

template <>
struct ratio_string<atto, char32_t>
{
    static u32string symbol() {return u32string(1, U'a');}
    static u32string prefix()  {return u32string(U"atto");}
};

#endif

template <>
struct ratio_string<atto, wchar_t>
{
    static wstring symbol() {return wstring(1, L'a');}
    static wstring prefix()  {return wstring(L"atto");}
};

// femto

template <>
struct ratio_string<femto, char>
{
    static string symbol() {return string(1, 'f');}
    static string prefix()  {return string("femto");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<femto, char16_t>
{
    static u16string symbol() {return u16string(1, u'f');}
    static u16string prefix()  {return u16string(u"femto");}
};

template <>
struct ratio_string<femto, char32_t>
{
    static u32string symbol() {return u32string(1, U'f');}
    static u32string prefix()  {return u32string(U"femto");}
};

#endif

template <>
struct ratio_string<femto, wchar_t>
{
    static wstring symbol() {return wstring(1, L'f');}
    static wstring prefix()  {return wstring(L"femto");}
};

// pico

template <>
struct ratio_string<pico, char>
{
    static string symbol() {return string(1, 'p');}
    static string prefix()  {return string("pico");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<pico, char16_t>
{
    static u16string symbol() {return u16string(1, u'p');}
    static u16string prefix()  {return u16string(u"pico");}
};

template <>
struct ratio_string<pico, char32_t>
{
    static u32string symbol() {return u32string(1, U'p');}
    static u32string prefix()  {return u32string(U"pico");}
};

#endif

template <>
struct ratio_string<pico, wchar_t>
{
    static wstring symbol() {return wstring(1, L'p');}
    static wstring prefix()  {return wstring(L"pico");}
};

// nano

template <>
struct ratio_string<nano, char>
{
    static string symbol() {return string(1, 'n');}
    static string prefix()  {return string("nano");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<nano, char16_t>
{
    static u16string symbol() {return u16string(1, u'n');}
    static u16string prefix()  {return u16string(u"nano");}
};

template <>
struct ratio_string<nano, char32_t>
{
    static u32string symbol() {return u32string(1, U'n');}
    static u32string prefix()  {return u32string(U"nano");}
};

#endif

template <>
struct ratio_string<nano, wchar_t>
{
    static wstring symbol() {return wstring(1, L'n');}
    static wstring prefix()  {return wstring(L"nano");}
};

// micro

template <>
struct ratio_string<micro, char>
{
    static string symbol() {return string("\xC2\xB5");}
    static string prefix()  {return string("micro");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<micro, char16_t>
{
    static u16string symbol() {return u16string(1, u'\xB5');}
    static u16string prefix()  {return u16string(u"micro");}
};

template <>
struct ratio_string<micro, char32_t>
{
    static u32string symbol() {return u32string(1, U'\xB5');}
    static u32string prefix()  {return u32string(U"micro");}
};

#endif

template <>
struct ratio_string<micro, wchar_t>
{
    static wstring symbol() {return wstring(1, L'\xB5');}
    static wstring prefix()  {return wstring(L"micro");}
};

// milli

template <>
struct ratio_string<milli, char>
{
    static string symbol() {return string(1, 'm');}
    static string prefix()  {return string("milli");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<milli, char16_t>
{
    static u16string symbol() {return u16string(1, u'm');}
    static u16string prefix()  {return u16string(u"milli");}
};

template <>
struct ratio_string<milli, char32_t>
{
    static u32string symbol() {return u32string(1, U'm');}
    static u32string prefix()  {return u32string(U"milli");}
};

#endif

template <>
struct ratio_string<milli, wchar_t>
{
    static wstring symbol() {return wstring(1, L'm');}
    static wstring prefix()  {return wstring(L"milli");}
};

// centi

template <>
struct ratio_string<centi, char>
{
    static string symbol() {return string(1, 'c');}
    static string prefix()  {return string("centi");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<centi, char16_t>
{
    static u16string symbol() {return u16string(1, u'c');}
    static u16string prefix()  {return u16string(u"centi");}
};

template <>
struct ratio_string<centi, char32_t>
{
    static u32string symbol() {return u32string(1, U'c');}
    static u32string prefix()  {return u32string(U"centi");}
};

#endif

template <>
struct ratio_string<centi, wchar_t>
{
    static wstring symbol() {return wstring(1, L'c');}
    static wstring prefix()  {return wstring(L"centi");}
};

// deci

template <>
struct ratio_string<deci, char>
{
    static string symbol() {return string(1, 'd');}
    static string prefix()  {return string("deci");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<deci, char16_t>
{
    static u16string symbol() {return u16string(1, u'd');}
    static u16string prefix()  {return u16string(u"deci");}
};

template <>
struct ratio_string<deci, char32_t>
{
    static u32string symbol() {return u32string(1, U'd');}
    static u32string prefix()  {return u32string(U"deci");}
};

#endif

template <>
struct ratio_string<deci, wchar_t>
{
    static wstring symbol() {return wstring(1, L'd');}
    static wstring prefix()  {return wstring(L"deci");}
};

// deca

template <>
struct ratio_string<deca, char>
{
    static string symbol() {return string("da");}
    static string prefix()  {return string("deca");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<deca, char16_t>
{
    static u16string symbol() {return u16string(u"da");}
    static u16string prefix()  {return u16string(u"deca");}
};

template <>
struct ratio_string<deca, char32_t>
{
    static u32string symbol() {return u32string(U"da");}
    static u32string prefix()  {return u32string(U"deca");}
};

#endif

template <>
struct ratio_string<deca, wchar_t>
{
    static wstring symbol() {return wstring(L"da");}
    static wstring prefix()  {return wstring(L"deca");}
};

// hecto

template <>
struct ratio_string<hecto, char>
{
    static string symbol() {return string(1, 'h');}
    static string prefix()  {return string("hecto");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<hecto, char16_t>
{
    static u16string symbol() {return u16string(1, u'h');}
    static u16string prefix()  {return u16string(u"hecto");}
};

template <>
struct ratio_string<hecto, char32_t>
{
    static u32string symbol() {return u32string(1, U'h');}
    static u32string prefix()  {return u32string(U"hecto");}
};

#endif

template <>
struct ratio_string<hecto, wchar_t>
{
    static wstring symbol() {return wstring(1, L'h');}
    static wstring prefix()  {return wstring(L"hecto");}
};

// kilo

template <>
struct ratio_string<kilo, char>
{
    static string symbol() {return string(1, 'k');}
    static string prefix()  {return string("kilo");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<kilo, char16_t>
{
    static u16string symbol() {return u16string(1, u'k');}
    static u16string prefix()  {return u16string(u"kilo");}
};

template <>
struct ratio_string<kilo, char32_t>
{
    static u32string symbol() {return u32string(1, U'k');}
    static u32string prefix()  {return u32string(U"kilo");}
};

#endif

template <>
struct ratio_string<kilo, wchar_t>
{
    static wstring symbol() {return wstring(1, L'k');}
    static wstring prefix()  {return wstring(L"kilo");}
};

// mega

template <>
struct ratio_string<mega, char>
{
    static string symbol() {return string(1, 'M');}
    static string prefix()  {return string("mega");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<mega, char16_t>
{
    static u16string symbol() {return u16string(1, u'M');}
    static u16string prefix()  {return u16string(u"mega");}
};

template <>
struct ratio_string<mega, char32_t>
{
    static u32string symbol() {return u32string(1, U'M');}
    static u32string prefix()  {return u32string(U"mega");}
};

#endif

template <>
struct ratio_string<mega, wchar_t>
{
    static wstring symbol() {return wstring(1, L'M');}
    static wstring prefix()  {return wstring(L"mega");}
};

// giga

template <>
struct ratio_string<giga, char>
{
    static string symbol() {return string(1, 'G');}
    static string prefix()  {return string("giga");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<giga, char16_t>
{
    static u16string symbol() {return u16string(1, u'G');}
    static u16string prefix()  {return u16string(u"giga");}
};

template <>
struct ratio_string<giga, char32_t>
{
    static u32string symbol() {return u32string(1, U'G');}
    static u32string prefix()  {return u32string(U"giga");}
};

#endif

template <>
struct ratio_string<giga, wchar_t>
{
    static wstring symbol() {return wstring(1, L'G');}
    static wstring prefix()  {return wstring(L"giga");}
};

// tera

template <>
struct ratio_string<tera, char>
{
    static string symbol() {return string(1, 'T');}
    static string prefix()  {return string("tera");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<tera, char16_t>
{
    static u16string symbol() {return u16string(1, u'T');}
    static u16string prefix()  {return u16string(u"tera");}
};

template <>
struct ratio_string<tera, char32_t>
{
    static u32string symbol() {return u32string(1, U'T');}
    static u32string prefix()  {return u32string(U"tera");}
};

#endif

template <>
struct ratio_string<tera, wchar_t>
{
    static wstring symbol() {return wstring(1, L'T');}
    static wstring prefix()  {return wstring(L"tera");}
};

// peta

template <>
struct ratio_string<peta, char>
{
    static string symbol() {return string(1, 'P');}
    static string prefix()  {return string("peta");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<peta, char16_t>
{
    static u16string symbol() {return u16string(1, u'P');}
    static u16string prefix()  {return u16string(u"peta");}
};

template <>
struct ratio_string<peta, char32_t>
{
    static u32string symbol() {return u32string(1, U'P');}
    static u32string prefix()  {return u32string(U"peta");}
};

#endif

template <>
struct ratio_string<peta, wchar_t>
{
    static wstring symbol() {return wstring(1, L'P');}
    static wstring prefix()  {return wstring(L"peta");}
};

// exa

template <>
struct ratio_string<exa, char>
{
    static string symbol() {return string(1, 'E');}
    static string prefix()  {return string("exa");}
};

#if HAS_UNICODE_SUPPORT

template <>
struct ratio_string<exa, char16_t>
{
    static u16string symbol() {return u16string(1, u'E');}
    static u16string prefix()  {return u16string(u"exa");}
};

template <>
struct ratio_string<exa, char32_t>
{
    static u32string symbol() {return u32string(1, U'E');}
    static u32string prefix()  {return u32string(U"exa");}
};

#endif

template <>
struct ratio_string<exa, wchar_t>
{
    static wstring symbol() {return wstring(1, L'E');}
    static wstring prefix()  {return wstring(L"exa");}
};

//_LIBCPP_END_NAMESPACE_STD
}

#endif  // _RATIO_IO
