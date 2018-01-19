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

#include <ripple/beast/core/SystemStats.h>

#include <cstdlib>
#include <iterator>
#include <memory>

// Some basic tests, to keep an eye on things and make sure these types work ok
// on all platforms.

#ifndef __INTELLISENSE__
static_assert (sizeof (std::intptr_t) == sizeof (void*), "std::intptr_t must be the same size as void*");

static_assert (sizeof (std::int8_t) == 1,   "std::int8_t must be exactly 1 byte!");
static_assert (sizeof (std::int16_t) == 2,  "std::int16_t must be exactly 2 bytes!");
static_assert (sizeof (std::int32_t) == 4,  "std::int32_t must be exactly 4 bytes!");
static_assert (sizeof (std::int64_t) == 8,  "std::int64_t must be exactly 8 bytes!");

static_assert (sizeof (std::uint8_t) == 1,  "std::uint8_t must be exactly 1 byte!");
static_assert (sizeof (std::uint16_t) == 2, "std::uint16_t must be exactly 2 bytes!");
static_assert (sizeof (std::uint32_t) == 4, "std::uint32_t must be exactly 4 bytes!");
static_assert (sizeof (std::uint64_t) == 8, "std::uint64_t must be exactly 8 bytes!");
#endif

namespace beast
{
//==============================================================================
std::vector <std::string>
getStackBacktrace()
{
    std::vector <std::string> result;

#if BEAST_ANDROID || BEAST_MINGW || BEAST_BSD
    assert(false); // sorry, not implemented yet!

#elif BEAST_WINDOWS
    HANDLE process = GetCurrentProcess();
    SymInitialize (process, nullptr, TRUE);

    void* stack[128];
    int frames = (int) CaptureStackBackTrace (0,
        std::distance(std::begin(stack), std::end(stack)),
        stack, nullptr);

    // Allow symbols that are up to 1024 characters long.
    std::size_t constexpr nameLength = 1024;

    alignas(SYMBOL_INFO) unsigned char symbuf[
        sizeof(SYMBOL_INFO) + nameLength * sizeof(SYMBOL_INFO::Name)];

    auto symbol = reinterpret_cast<SYMBOL_INFO*>(symbuf);

    for (int i = 0; i < frames; ++i)
    {
        DWORD64 displacement = 0;

        std::memset (symbol, 0, sizeof(symbuf));

        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = nameLength;

        if (SymFromAddr (process, (DWORD64)stack[i], &displacement, symbol))
        {
            std::string frame;

            frame.append (std::to_string (i) + ": ");

            IMAGEHLP_MODULE64 moduleInfo { sizeof(moduleInfo) };

            if (::SymGetModuleInfo64 (process, symbol->ModBase, &moduleInfo))
            {
                frame.append (moduleInfo.ModuleName);
                frame.append (": ");
            }

            frame.append (symbol->Name);

            if (displacement)
            {
                frame.append ("+");
                frame.append (std::to_string (displacement));
            }

            result.push_back (frame);
        }
    }

#else
    void* stack[128];
    int frames = backtrace (stack,
        std::distance(std::begin(stack), std::end(stack)));

    std::unique_ptr<char*[], decltype(std::free)*> frame {
        backtrace_symbols (stack, frames), std::free };

    for (int i = 0; i < frames; ++i)
        result.push_back (frame[i]);
#endif

    return result;
}

} // beast
