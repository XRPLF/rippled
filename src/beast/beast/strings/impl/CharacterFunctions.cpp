//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#if BEAST_INCLUDE_BEASTCONFIG
#include "../../BeastConfig.h"
#endif

#include <beast/strings/CharacterFunctions.h>

#include <cctype>
#include <cwctype>

namespace beast {

#if BEAST_MSVC
 #pragma warning (push)
 #pragma warning (disable: 4514 4996)
#endif

beast_wchar CharacterFunctions::toUpperCase (const beast_wchar character) noexcept
{
    return towupper ((wchar_t) character);
}

beast_wchar CharacterFunctions::toLowerCase (const beast_wchar character) noexcept
{
    return towlower ((wchar_t) character);
}

bool CharacterFunctions::isUpperCase (const beast_wchar character) noexcept
{
   #if BEAST_WINDOWS
    return iswupper ((wchar_t) character) != 0;
   #else
    return toLowerCase (character) != character;
   #endif
}

bool CharacterFunctions::isLowerCase (const beast_wchar character) noexcept
{
   #if BEAST_WINDOWS
    return iswlower ((wchar_t) character) != 0;
   #else
    return toUpperCase (character) != character;
   #endif
}

#if BEAST_MSVC
 #pragma warning (pop)
#endif

//==============================================================================
bool CharacterFunctions::isWhitespace (const char character) noexcept
{
    return character == ' ' || (character <= 13 && character >= 9);
}

bool CharacterFunctions::isWhitespace (const beast_wchar character) noexcept
{
    return iswspace ((wchar_t) character) != 0;
}

bool CharacterFunctions::isDigit (const char character) noexcept
{
    return (character >= '0' && character <= '9');
}

bool CharacterFunctions::isDigit (const beast_wchar character) noexcept
{
    return iswdigit ((wchar_t) character) != 0;
}

bool CharacterFunctions::isLetter (const char character) noexcept
{
    return (character >= 'a' && character <= 'z')
        || (character >= 'A' && character <= 'Z');
}

bool CharacterFunctions::isLetter (const beast_wchar character) noexcept
{
    return iswalpha ((wchar_t) character) != 0;
}

bool CharacterFunctions::isLetterOrDigit (const char character) noexcept
{
    return (character >= 'a' && character <= 'z')
        || (character >= 'A' && character <= 'Z')
        || (character >= '0' && character <= '9');
}

bool CharacterFunctions::isLetterOrDigit (const beast_wchar character) noexcept
{
    return iswalnum ((wchar_t) character) != 0;
}

int CharacterFunctions::getHexDigitValue (const beast_wchar digit) noexcept
{
    unsigned int d = (unsigned int) digit - '0';
    if (d < (unsigned int) 10)
        return (int) d;

    d += (unsigned int) ('0' - 'a');
    if (d < (unsigned int) 6)
        return (int) d + 10;

    d += (unsigned int) ('a' - 'A');
    if (d < (unsigned int) 6)
        return (int) d + 10;

    return -1;
}

double CharacterFunctions::mulexp10 (const double value, int exponent) noexcept
{
    if (exponent == 0)
        return value;

    if (value == 0)
        return 0;

    const bool negative = (exponent < 0);
    if (negative)
        exponent = -exponent;

    double result = 1.0, power = 10.0;
    for (int bit = 1; exponent != 0; bit <<= 1)
    {
        if ((exponent & bit) != 0)
        {
            exponent ^= bit;
            result *= power;
            if (exponent == 0)
                break;
        }
        power *= power;
    }

    return negative ? (value / result) : (value * result);
}

}
