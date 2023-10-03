//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/basics/ThreadUtilities.h>
#include <stdexcept>

namespace ripple {

#ifdef __APPLE__

#include <pthread.h>

std::string
get_name(std::thread::native_handle_type t)
{
    char buffer[64];
    if (pthread_getname_np(t, buffer, sizeof(buffer)) != 0)
        throw std::runtime_error("get_name failed\n");
    return buffer;
}

namespace this_thread {

std::string
get_name()
{
    return ripple::get_name(pthread_self());
}

void
set_name(std::string s)
{
    s.resize(15);
    if (pthread_setname_np(s.data()) != 0)
        throw std::runtime_error("this_thread::set_name failed\n");
}

}  // namespace this_thread

#endif  // __APPLE__

#ifdef __linux__

#include <pthread.h>

std::string
get_name(std::thread::native_handle_type t)
{
    char buffer[64];
    if (pthread_getname_np(t, buffer, sizeof(buffer)) != 0)
        throw std::runtime_error("get_name failed\n");
    return buffer;
}

namespace this_thread {

std::string
get_name()
{
    return ripple::get_name(pthread_self());
}

void
set_name(std::string s)
{
    s.resize(15);
    if (pthread_setname_np(pthread_self(), s.data()) != 0)
        throw std::runtime_error("this_thread::set_name failed\n");
}

}  // namespace this_thread

#endif  // __linux__

#ifdef _WIN64

#define WIN32_LEAN_AND_MEAN

#include <memory>
#include <processthreadsapi.h>
#include <windows.h>

std::string
get_name(std::thread::native_handle_type t)
{
    wchar_t* unhandled_data{};
    HRESULT r = GetThreadDescription(t, &unhandled_data);
    std::unique_ptr<wchar_t, HLOCAL (*)(HLOCAL)> data{
        unhandled_data, LocalFree};
    if (FAILED(r))
        throw std::runtime_error("get_name failed\n");
    std::string s;
    auto p = data.get();
    while (*p)
        s.push_back(static_cast<char>(*p++));
    return s;
}

namespace this_thread {

std::string
get_name()
{
    return ripple::get_name(GetCurrentThread());
}

void
set_name(std::string s)
{
    assert(s.size() <= 15);
    s.resize(15);
    std::wstring ws;
    for (auto c : s)
        ws += c;
    HRESULT r = SetThreadDescription(GetCurrentThread(), ws.data());
    if (FAILED(r))
        throw std::runtime_error("this_thread::set_name failed\n");
}

}  // namespace this_thread

#endif  // __WINDOWS__

}  // namespace ripple
