//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.beast.com

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

#ifndef BEAST_STRINGS_STRINGFROMNUMBER_H_INCLUDED
#define BEAST_STRINGS_STRINGFROMNUMBER_H_INCLUDED

#include <beast/Config.h>
#include <beast/Arithmetic.h>

#include <beast/strings/StringCharPointerType.h>

#include <limits>
#include <ostream>
#include <streambuf>

namespace beast {

// VFALCO TODO Put this in namespace detail
//
class NumberToStringConverters
{
public:
    enum
    {
        charsNeededForInt = 32,
        charsNeededForDouble = 48
    };

    // pass in a pointer to the END of a buffer..
    template <typename Type>
    static char* printDigits (char* t, Type v) noexcept
    {
        *--t = 0;
        do
        {
            *--t = '0' + (char) (v % 10);
            v /= 10;
        }
        while (v > 0);
        return t;
    }

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4127) // conditional expression is constant
#pragma warning (disable: 4146) // unary minus operator applied to unsigned type, result still unsigned
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
    // pass in a pointer to the END of a buffer..
    template <typename IntegerType>
    static char* numberToString (char* t, IntegerType const n) noexcept
    {
        if (std::numeric_limits <IntegerType>::is_signed)
        {
            if (n >= 0)
                return printDigits (t, static_cast <std::uint64_t> (n));

            // NB: this needs to be careful not to call
            // -std::numeric_limits<std::int64_t>::min(),
            // which has undefined behaviour
            //
            t = printDigits (t, static_cast <std::uint64_t> (-(n + 1)) + 1);
            *--t = '-';
            return t;
        }
        return printDigits (t, n);
    }
#ifdef _MSC_VER
#pragma warning (pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

    struct StackArrayStream : public std::basic_streambuf <char, std::char_traits <char> >
    {
        explicit StackArrayStream (char* d)
        {
            imbue (std::locale::classic());
            setp (d, d + charsNeededForDouble);
        }

        size_t writeDouble (double n, int numDecPlaces)
        {
            {
                std::ostream o (this);

                if (numDecPlaces > 0)
                    o.precision ((std::streamsize) numDecPlaces);

                o << n;
            }

            return (size_t) (pptr() - pbase());
        }
    };

    static char* doubleToString (char* buffer,
        const int numChars, double n, int numDecPlaces, size_t& len) noexcept
    {
        if (numDecPlaces > 0 && numDecPlaces < 7 && n > -1.0e20 && n < 1.0e20)
        {
            char* const end = buffer + numChars;
            char* t = end;
            std::int64_t v = (std::int64_t) (pow (10.0, numDecPlaces) * std::abs (n) + 0.5);
            *--t = (char) 0;

            while (numDecPlaces >= 0 || v > 0)
            {
                if (numDecPlaces == 0)
                    *--t = '.';

                *--t = (char) ('0' + (v % 10));

                v /= 10;
                --numDecPlaces;
            }

            if (n < 0)
                *--t = '-';

            len = (size_t) (end - t - 1);
            return t;
        }

        StackArrayStream strm (buffer);
        len = strm.writeDouble (n, numDecPlaces);
        bassert (len <= charsNeededForDouble);
        return buffer;
    }

    static StringCharPointerType createFromFixedLength (
        const char* const src, const size_t numChars);

    template <typename IntegerType>
    static StringCharPointerType createFromInteger (const IntegerType number)
    {
        char buffer [charsNeededForInt];
        char* const end = buffer + numElementsInArray (buffer);
        char* const start = numberToString (end, number);
        return createFromFixedLength (start, (size_t) (end - start - 1));
    }

    static StringCharPointerType createFromDouble (
        const double number, const int numberOfDecimalPlaces)
    {
        char buffer [charsNeededForDouble];
        size_t len;
        char* const start = doubleToString (buffer, numElementsInArray (buffer), (double) number, numberOfDecimalPlaces, len);
        return createFromFixedLength (start, len);
    }
};

}

#endif
