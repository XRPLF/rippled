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

//  chrono_io
//
//  (C) Copyright Howard Hinnant
//  Use, modification and distribution are subject to the Boost Software License,
//  Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt).

#ifndef BEAST_CHRONO_CHRONO_IO_H_INCLUDED
#define BEAST_CHRONO_CHRONO_IO_H_INCLUDED

#include <beast/Config.h>

#include <ctime>
#include <locale>

/*

    chrono_io synopsis

#include <chrono>
#include <ratio_io>

namespace std
{
namespace chrono
{

enum duration_style {prefix, symbol};
enum timezone {utc, local};

// facets

class durationpunct
    : public locale::facet
{
public:
    static locale::id id;

    explicit durationpunct(size_t refs = 0);
    explicit durationpunct(duration_style fmt, size_t refs = 0);

    bool is_symbol_name() const noexcept;
    bool is_prefix_name() const noexcept;
};

template <class charT>
class timepunct
    : public locale::facet
{
public:
    typedef basic_string<charT> string_type;

    static locale::id id;

    explicit timepunct(size_t refs = 0);
    timepunct(timezone tz, string_type fmt, size_t refs = 0);

    const string_type& fmt() const noexcept;
    std::chrono::timezone timezone() const noexcept;
};

// manipulators

class duration_fmt
{
public:
    explicit duration_fmt(duration_style f) noexcept;
    explicit operator duration_style() const noexcept;
};

unspecified time_fmt(timezone tz);
template<class charT>
    unspecified time_fmt(timezone tz, basic_string<charT> fmt);
template<class charT>
    unspecified time_fmt(timezone tz, const charT* fmt);

template<class charT, class traits>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, duration_fmt d);

template<class charT, class traits>
std::basic_istream<charT, traits>&
operator>>(std::basic_istream<charT, traits>& is, duration_fmt d);

// duration I/O

template <class charT, class Traits, class Rep, class Period>
    basic_ostream<charT, Traits>&
    operator<<(basic_ostream<charT, Traits>& os, const duration<Rep, Period>& d);

template <class charT, class Traits, class Rep, class Period>
    basic_istream<charT, Traits>&
    operator>>(basic_istream<charT, Traits>& is, duration<Rep, Period>& d);

// system_clock I/O

template <class charT, class Traits, class Duration>
    basic_ostream<charT, Traits>&
    operator<<(basic_ostream<charT, Traits>& os,
               const time_point<system_clock, Duration>& tp);

template <class charT, class Traits, class Duration>
    basic_istream<charT, Traits>&
    operator>>(basic_istream<charT, Traits>& is,
               time_point<system_clock, Duration>& tp);

// steady_clock I/O

template <class charT, class Traits, class Duration>
    basic_ostream<charT, Traits>&
    operator<<(basic_ostream<charT, Traits>& os,
               const time_point<steady_clock, Duration>& tp);

template <class charT, class Traits, class Duration>
    basic_istream<charT, Traits>&
    operator>>(basic_istream<charT, Traits>& is,
               time_point<steady_clock, Duration>& tp);

// high_resolution_clock I/O

template <class charT, class Traits, class Duration>
    basic_ostream<charT, Traits>&
    operator<<(basic_ostream<charT, Traits>& os,
               const time_point<high_resolution_clock, Duration>& tp);

template <class charT, class Traits, class Duration>
    basic_istream<charT, Traits>&
    operator>>(basic_istream<charT, Traits>& is,
               time_point<high_resolution_clock, Duration>& tp);

}  // chrono
}  // std

*/

#include <chrono>
#include <beast/chrono/ratio_io.h>

//_LIBCPP_BEGIN_NAMESPACE_STD
namespace std {

namespace chrono
{

template <class To, class Rep, class Period>
To
round(const duration<Rep, Period>& d)
{
    To t0 = duration_cast<To>(d);
    To t1 = t0;
    ++t1;
    typedef typename common_type<To, duration<Rep, Period> >::type _D;
    _D diff0 = d - t0;
    _D diff1 = t1 - d;
    if (diff0 == diff1)
    {
        if (t0.count() & 1)
            return t1;
        return t0;
    }
    else if (diff0 < diff1)
        return t0;
    return t1;
}

enum duration_style {prefix, symbol};
enum timezone {utc, local};

class durationpunct
    : public locale::facet
{
private:
    duration_style __style_;
public:
    static locale::id id;

    explicit durationpunct(size_t refs = 0)
        : locale::facet(refs), __style_(prefix) {}

    explicit durationpunct(duration_style fmt, size_t refs = 0)
        : locale::facet(refs), __style_(fmt) {}

    bool is_symbol_name() const noexcept {return __style_ == symbol;}
    bool is_prefix_name() const noexcept {return __style_ == prefix;}
};

class duration_fmt
{
    duration_style form_;
public:
    explicit duration_fmt(duration_style f) noexcept : form_(f) {}
    // VFALCO NOTE disabled this for MSVC
    /*explicit*/
        operator duration_style() const noexcept {return form_;}
};

template<class charT, class traits>
basic_ostream<charT, traits>&
operator <<(basic_ostream<charT, traits>& os, duration_fmt d)
{
    os.imbue(locale(os.getloc(), new durationpunct(static_cast<duration_style>(d))));
    return os;
}

template<class charT, class traits>
basic_istream<charT, traits>&
operator >>(basic_istream<charT, traits>& is, duration_fmt d)
{
    is.imbue(locale(is.getloc(), new durationpunct(static_cast<duration_style>(d))));
    return is;
}

template <class _CharT, class _Rep, class _Period>
basic_string<_CharT>
__get_unit(bool __is_long, const duration<_Rep, _Period>& d)
{
    if (__is_long)
    {
        _CharT __p[] = {'s', 'e', 'c', 'o', 'n', 'd', 's', 0};
        basic_string<_CharT> s = ratio_string<_Period, _CharT>::prefix() + __p;
        if (d.count() == 1 || d.count() == -1)
            s.pop_back();
        return s;
    }
    return ratio_string<_Period, _CharT>::symbol() + 's';
}

template <class _CharT, class _Rep>
basic_string<_CharT>
__get_unit(bool __is_long, const duration<_Rep, ratio<1> >& d)
{
    if (__is_long)
    {
        _CharT __p[] = {'s', 'e', 'c', 'o', 'n', 'd', 's'};
        basic_string<_CharT> s = basic_string<_CharT>(__p, __p + sizeof(__p) / sizeof(_CharT));
        if (d.count() == 1 || d.count() == -1)
            s.pop_back();
        return s;
    }
    return basic_string<_CharT>(1, 's');
}

template <class _CharT, class _Rep>
basic_string<_CharT>
__get_unit(bool __is_long, const duration<_Rep, ratio<60> >& d)
{
    if (__is_long)
    {
        _CharT __p[] = {'m', 'i', 'n', 'u', 't', 'e', 's'};
        basic_string<_CharT> s = basic_string<_CharT>(__p, __p + sizeof(__p) / sizeof(_CharT));
        if (d.count() == 1 || d.count() == -1)
            s.pop_back();
        return s;
    }
    _CharT __p[] = {'m', 'i', 'n'};
    return basic_string<_CharT>(__p, __p + sizeof(__p) / sizeof(_CharT));
}

template <class _CharT, class _Rep>
basic_string<_CharT>
__get_unit(bool __is_long, const duration<_Rep, ratio<3600> >& d)
{
    if (__is_long)
    {
        _CharT __p[] = {'h', 'o', 'u', 'r', 's'};
        basic_string<_CharT> s = basic_string<_CharT>(__p, __p + sizeof(__p) / sizeof(_CharT));
        if (d.count() == 1 || d.count() == -1)
            s.pop_back();
        return s;
    }
    return basic_string<_CharT>(1, 'h');
}

template <class _CharT, class _Traits, class _Rep, class _Period>
basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const duration<_Rep, _Period>& __d)
{
    typename basic_ostream<_CharT, _Traits>::sentry ok(__os);
    if (ok)
    {
        typedef durationpunct _F;
        typedef basic_string<_CharT> string_type;
        bool failed = false;
        try
        {
            bool __is_long = true;
            locale __loc = __os.getloc();
            if (has_facet<_F>(__loc))
            {
                const _F& f = use_facet<_F>(__loc);
                __is_long = f.is_prefix_name();
            }
            string_type __unit = __get_unit<_CharT>(__is_long, __d);
            __os << __d.count() << ' ' << __unit;
        }
        catch (...)
        {
            failed = true;
        }
        if (failed)
            __os.setstate(ios_base::failbit | ios_base::badbit);
    }
    return __os;
}

template <class _Rep, bool = is_scalar<_Rep>::value>
struct __duration_io_intermediate
{
    typedef _Rep type;
};

template <class _Rep>
struct __duration_io_intermediate<_Rep, true>
{
    typedef typename conditional
    <
        is_floating_point<_Rep>::value,
            long double,
            typename conditional
            <
                is_signed<_Rep>::value,
                    long long,
                    unsigned long long
            >::type
    >::type type;
};

template <class T>
T
__gcd(T x, T y)
{
    while (y != 0)
    {
        T old_x = x;
        x = y;
        y = old_x % y;
    }
    return x;
}

template <>
long double
inline
__gcd(long double, long double)
{
    return 1;
}

template <class _CharT, class _Traits, class _Rep, class _Period>
basic_istream<_CharT, _Traits>&
operator>>(basic_istream<_CharT, _Traits>& __is, duration<_Rep, _Period>& __d)
{
    // These are unused and generate warnings
    //typedef basic_string<_CharT> string_type;
    //typedef durationpunct _F;

    typedef typename __duration_io_intermediate<_Rep>::type _IR;
    _IR __r;
    // read value into __r
    __is >> __r;
    if (__is.good())
    {
        // now determine unit
        typedef istreambuf_iterator<_CharT, _Traits> _I;
        _I __i(__is);
        _I __e;
        if (__i != __e && *__i == ' ')  // mandatory ' ' after value
        {
            ++__i;
            if (__i != __e)
            {
                locale __loc = __is.getloc();
                // unit is num / den (yet to be determined)
                unsigned long long num = 0;
                unsigned long long den = 0;
                ios_base::iostate __err = ios_base::goodbit;
                if (*__i == '[')
                {
                    // parse [N/D]s or [N/D]seconds format
                    ++__i;
                    _CharT __x;
                    __is >> num >> __x >> den;
                    if (!__is.good() || __x != '/')
                    {
                        __is.setstate(__is.failbit);
                        return __is;
                    }
                    __i = _I(__is);
                    if (*__i != ']')
                    {
                        __is.setstate(__is.failbit);
                        return __is;
                    }
                    ++__i;
                    const basic_string<_CharT> __units[] =
                    {
                        __get_unit<_CharT>(true, seconds(2)),
                        __get_unit<_CharT>(true, seconds(1)),
                        __get_unit<_CharT>(false, seconds(1))
                    };
                    const basic_string<_CharT>* __k = __scan_keyword(__i, __e,
                                  __units, __units + sizeof(__units)/sizeof(__units[0]),
                                  use_facet<ctype<_CharT> >(__loc),
                                  __err);
                    switch ((__k - __units) / 3)
                    {
                    case 0:
                        break;
                    default:
                        __is.setstate(__err);
                        return __is;
                    }
                }
                else
                {
                    // parse SI name, short or long
                    const basic_string<_CharT> __units[] =
                    {
                        __get_unit<_CharT>(true, duration<_Rep, atto>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, atto>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, atto>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, femto>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, femto>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, femto>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, pico>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, pico>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, pico>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, nano>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, nano>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, nano>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, micro>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, micro>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, micro>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, milli>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, milli>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, milli>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, centi>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, centi>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, centi>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, deci>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, deci>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, deci>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, deca>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, deca>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, deca>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, hecto>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, hecto>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, hecto>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, kilo>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, kilo>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, kilo>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, mega>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, mega>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, mega>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, giga>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, giga>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, giga>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, tera>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, tera>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, tera>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, peta>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, peta>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, peta>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, exa>(2)),
                        __get_unit<_CharT>(true, duration<_Rep, exa>(1)),
                        __get_unit<_CharT>(false, duration<_Rep, exa>(1)),
                        __get_unit<_CharT>(true, duration<_Rep, ratio<1> >(2)),
                        __get_unit<_CharT>(true, duration<_Rep, ratio<1> >(1)),
                        __get_unit<_CharT>(false, duration<_Rep, ratio<1> >(1)),
                        __get_unit<_CharT>(true, duration<_Rep, ratio<60> >(2)),
                        __get_unit<_CharT>(true, duration<_Rep, ratio<60> >(1)),
                        __get_unit<_CharT>(false, duration<_Rep, ratio<60> >(1)),
                        __get_unit<_CharT>(true, duration<_Rep, ratio<3600> >(2)),
                        __get_unit<_CharT>(true, duration<_Rep, ratio<3600> >(1)),
                        __get_unit<_CharT>(false, duration<_Rep, ratio<3600> >(1))
                    };
                    const basic_string<_CharT>* __k = __scan_keyword(__i, __e,
                                  __units, __units + sizeof(__units)/sizeof(__units[0]),
                                  use_facet<ctype<_CharT> >(__loc),
                                  __err);
                    switch (__k - __units)
                    {
                    case 0:
                    case 1:
                    case 2:
                        num = 1ULL;
                        den = 1000000000000000000ULL;
                        break;
                    case 3:
                    case 4:
                    case 5:
                        num = 1ULL;
                        den = 1000000000000000ULL;
                        break;
                    case 6:
                    case 7:
                    case 8:
                        num = 1ULL;
                        den = 1000000000000ULL;
                        break;
                    case 9:
                    case 10:
                    case 11:
                        num = 1ULL;
                        den = 1000000000ULL;
                        break;
                    case 12:
                    case 13:
                    case 14:
                        num = 1ULL;
                        den = 1000000ULL;
                        break;
                    case 15:
                    case 16:
                    case 17:
                        num = 1ULL;
                        den = 1000ULL;
                        break;
                    case 18:
                    case 19:
                    case 20:
                        num = 1ULL;
                        den = 100ULL;
                        break;
                    case 21:
                    case 22:
                    case 23:
                        num = 1ULL;
                        den = 10ULL;
                        break;
                    case 24:
                    case 25:
                    case 26:
                        num = 10ULL;
                        den = 1ULL;
                        break;
                    case 27:
                    case 28:
                    case 29:
                        num = 100ULL;
                        den = 1ULL;
                        break;
                    case 30:
                    case 31:
                    case 32:
                        num = 1000ULL;
                        den = 1ULL;
                        break;
                    case 33:
                    case 34:
                    case 35:
                        num = 1000000ULL;
                        den = 1ULL;
                        break;
                    case 36:
                    case 37:
                    case 38:
                        num = 1000000000ULL;
                        den = 1ULL;
                        break;
                    case 39:
                    case 40:
                    case 41:
                        num = 1000000000000ULL;
                        den = 1ULL;
                        break;
                    case 42:
                    case 43:
                    case 44:
                        num = 1000000000000000ULL;
                        den = 1ULL;
                        break;
                    case 45:
                    case 46:
                    case 47:
                        num = 1000000000000000000ULL;
                        den = 1ULL;
                        break;
                    case 48:
                    case 49:
                    case 50:
                        num = 1;
                        den = 1;
                        break;
                    case 51:
                    case 52:
                    case 53:
                        num = 60;
                        den = 1;
                        break;
                    case 54:
                    case 55:
                    case 56:
                        num = 3600;
                        den = 1;
                        break;
                    default:
                        __is.setstate(__err);
                        return __is;
                    }
                }
                // unit is num/den
                // __r should be multiplied by (num/den) / _Period
                // Reduce (num/den) / _Period to lowest terms
                unsigned long long __gcd_n1_n2 = __gcd<unsigned long long>(num, _Period::num);
                unsigned long long __gcd_d1_d2 = __gcd<unsigned long long>(den, _Period::den);
                num /= __gcd_n1_n2;
                den /= __gcd_d1_d2;
                unsigned long long __n2 = _Period::num / __gcd_n1_n2;
                unsigned long long __d2 = _Period::den / __gcd_d1_d2;
                if (num > numeric_limits<unsigned long long>::max() / __d2 ||
                    den > numeric_limits<unsigned long long>::max() / __n2)
                {
                    // (num/den) / _Period overflows
                    __is.setstate(__is.failbit);
                    return __is;
                }
                num *= __d2;
                den *= __n2;
                // num / den is now factor to multiply by __r
                typedef typename common_type<_IR, unsigned long long>::type _CT;
                if (is_integral<_IR>::value)
                {
                    // Reduce __r * num / den
                    _CT __t = __gcd<_CT>(__r, den);
                    __r /= __t;
                    den /= __t;
                    if (den != 1)
                    {
                        // Conversion to _Period is integral and not exact
                        __is.setstate(__is.failbit);
                        return __is;
                    }
                }
                if (__r > duration_values<_CT>::max() / num)
                {
                    // Conversion to _Period overflowed
                    __is.setstate(__is.failbit);
                    return __is;
                }
                _CT __t = __r * num;
                __t /= den;
                if (duration_values<_Rep>::max() < __t)
                {
                    // Conversion to _Period overflowed
                    __is.setstate(__is.failbit);
                    return __is;
                }
                // Success!  Store it.
                __r = _Rep(__t);
                __d = duration<_Rep, _Period>(__r);
                __is.setstate(__err);
            }
            else
                __is.setstate(__is.failbit | __is.eofbit);
        }
        else
        {
            if (__i == __e)
                __is.setstate(__is.eofbit);
            __is.setstate(__is.failbit);
        }
    }
    else
        __is.setstate(__is.failbit);
    return __is;
}

template <class charT>
class timepunct
    : public locale::facet
{
public:
    typedef basic_string<charT> string_type;

private:
    string_type           fmt_;
    chrono::timezone tz_;

public:
    static locale::id id;

    explicit timepunct(size_t refs = 0)
        : locale::facet(refs), tz_(utc) {}
    timepunct(timezone tz, string_type fmt, size_t refs = 0)
        : locale::facet(refs), fmt_(std::move(fmt)), tz_(tz) {}

    const string_type& fmt() const noexcept {return fmt_;}
    chrono::timezone get_timezone() const noexcept {return tz_;}
};

template <class CharT>
locale::id
timepunct<CharT>::id;

template <class _CharT, class _Traits, class _Duration>
basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os,
           const time_point<steady_clock, _Duration>& __tp)
{
    return __os << __tp.time_since_epoch() << " since boot";
}

template<class charT>
struct __time_manip
{
    basic_string<charT> fmt_;
    timezone tz_;

    __time_manip(timezone tz, basic_string<charT> fmt)
        : fmt_(std::move(fmt)),
          tz_(tz) {}
};

template<class charT, class traits>
basic_ostream<charT, traits>&
operator <<(basic_ostream<charT, traits>& os, __time_manip<charT> m)
{
    os.imbue(locale(os.getloc(), new timepunct<charT>(m.tz_, std::move(m.fmt_))));
    return os;
}

template<class charT, class traits>
basic_istream<charT, traits>&
operator >>(basic_istream<charT, traits>& is, __time_manip<charT> m)
{
    is.imbue(locale(is.getloc(), new timepunct<charT>(m.tz_, std::move(m.fmt_))));
    return is;
}

template<class charT>
inline
__time_manip<charT>
time_fmt(timezone tz, const charT* fmt)
{
    return __time_manip<charT>(tz, fmt);
}

template<class charT>
inline
__time_manip<charT>
time_fmt(timezone tz, basic_string<charT> fmt)
{
    return __time_manip<charT>(tz, std::move(fmt));
}

class __time_man
{
    timezone form_;
public:
    explicit __time_man(timezone f) : form_(f) {}
    // explicit
        operator timezone() const {return form_;}
};

template<class charT, class traits>
basic_ostream<charT, traits>&
operator <<(basic_ostream<charT, traits>& os, __time_man m)
{
    os.imbue(locale(os.getloc(), new timepunct<charT>(static_cast<timezone>(m), basic_string<charT>())));
    return os;
}

template<class charT, class traits>
basic_istream<charT, traits>&
operator >>(basic_istream<charT, traits>& is, __time_man m)
{
    is.imbue(locale(is.getloc(), new timepunct<charT>(static_cast<timezone>(m), basic_string<charT>())));
    return is;
}

inline
__time_man
time_fmt(timezone f)
{
    return __time_man(f);
}

} // chrono

}

#endif

