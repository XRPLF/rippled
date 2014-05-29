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

#ifndef BEAST_STRINGS_STRINGCHARPOINTERTYPE_H_INCLUDED
#define BEAST_STRINGS_STRINGCHARPOINTERTYPE_H_INCLUDED

#include <beast/Config.h>
#include <beast/strings/CharPointer_UTF8.h>
#include <beast/strings/CharPointer_UTF16.h>
#include <beast/strings/CharPointer_UTF32.h>

namespace beast {

/** This is the character encoding type used internally to store the string.

    By setting the value of BEAST_STRING_UTF_TYPE to 8, 16, or 32, you can change the
    internal storage format of the String class. UTF-8 uses the least space (if your strings
    contain few extended characters), but call operator[] involves iterating the string to find
    the required index. UTF-32 provides instant random access to its characters, but uses 4 bytes
    per character to store them. UTF-16 uses more space than UTF-8 and is also slow to index,
    but is the native wchar_t format used in Windows.

    It doesn't matter too much which format you pick, because the toUTF8(), toUTF16() and
    toUTF32() methods let you access the string's content in any of the other formats.
*/
#if (BEAST_STRING_UTF_TYPE == 32)
typedef CharPointer_UTF32 StringCharPointerType;

#elif (BEAST_STRING_UTF_TYPE == 16)
typedef CharPointer_UTF16 StringCharPointerType;

#elif (BEAST_STRING_UTF_TYPE == 8)
typedef CharPointer_UTF8  StringCharPointerType;

#else
#error "You must set the value of BEAST_STRING_UTF_TYPE to be either 8, 16, or 32!"

#endif

}

#endif

