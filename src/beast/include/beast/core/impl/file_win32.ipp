//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_CORE_IMPL_FILE_WIN32_IPP
#define BEAST_CORE_IMPL_FILE_WIN32_IPP

#include <boost/detail/winapi/access_rights.hpp>
#include <boost/detail/winapi/error_codes.hpp>
#include <boost/detail/winapi/file_management.hpp>
#include <boost/detail/winapi/get_last_error.hpp>
#include <limits>
#include <utility>

namespace beast {

namespace detail {

// VFALCO Can't seem to get boost/detail/winapi to work with
//        this so use the non-Ex version for now.
inline
boost::detail::winapi::BOOL_
set_file_pointer_ex(
    boost::detail::winapi::HANDLE_ hFile,
    boost::detail::winapi::LARGE_INTEGER_ lpDistanceToMove,
    boost::detail::winapi::PLARGE_INTEGER_ lpNewFilePointer,
    boost::detail::winapi::DWORD_ dwMoveMethod)
{
    auto dwHighPart = lpDistanceToMove.u.HighPart;
    auto dwLowPart = boost::detail::winapi::SetFilePointer(
        hFile,
        lpDistanceToMove.u.LowPart,
        &dwHighPart,
        dwMoveMethod);
    if(dwLowPart == boost::detail::winapi::INVALID_SET_FILE_POINTER_)
        return 0;
    if(lpNewFilePointer)
    {
        lpNewFilePointer->u.LowPart = dwLowPart;
        lpNewFilePointer->u.HighPart = dwHighPart;
    }
    return 1;
}

} // detail

inline
file_win32::
~file_win32()
{
    if(h_ != boost::detail::winapi::INVALID_HANDLE_VALUE_)
        boost::detail::winapi::CloseHandle(h_);
}

inline
file_win32::
file_win32(file_win32&& other)
    : h_(other.h_)
{
    other.h_ = boost::detail::winapi::INVALID_HANDLE_VALUE_;
}

inline
file_win32&
file_win32::
operator=(file_win32&& other)
{
    if(&other == this)
        return *this;
    if(h_)
        boost::detail::winapi::CloseHandle(h_);
    h_ = other.h_;
    other.h_ = boost::detail::winapi::INVALID_HANDLE_VALUE_;
    return *this;
}

inline
void
file_win32::
native_handle(native_handle_type h)
{
     if(h_ != boost::detail::winapi::INVALID_HANDLE_VALUE_)
        boost::detail::winapi::CloseHandle(h_);
    h_ = h;
}

inline
void
file_win32::
close(error_code& ec)
{
    if(h_ != boost::detail::winapi::INVALID_HANDLE_VALUE_)
    {
        if(! boost::detail::winapi::CloseHandle(h_))
            ec.assign(boost::detail::winapi::GetLastError(),
                system_category());
        else
            ec.assign(0, ec.category());
        h_ = boost::detail::winapi::INVALID_HANDLE_VALUE_;
    }
    else
    {
        ec.assign(0, ec.category());
    }
}

inline
void
file_win32::
open(char const* path, file_mode mode, error_code& ec)
{
    if(h_ != boost::detail::winapi::INVALID_HANDLE_VALUE_)
    {
        boost::detail::winapi::CloseHandle(h_);
        h_ = boost::detail::winapi::INVALID_HANDLE_VALUE_;
    }
    boost::detail::winapi::DWORD_ dw1 = 0;
    boost::detail::winapi::DWORD_ dw2 = 0;
    boost::detail::winapi::DWORD_ dw3 = 0;
/*
                             |                    When the file...
    This argument:           |             Exists            Does not exist
    -------------------------+------------------------------------------------------
    CREATE_ALWAYS            |            Truncates             Creates
    CREATE_NEW         +-----------+        Fails               Creates
    OPEN_ALWAYS     ===| does this |===>    Opens               Creates
    OPEN_EXISTING      +-----------+        Opens                Fails
    TRUNCATE_EXISTING        |            Truncates              Fails
*/
    switch(mode)
    {
    default:
    case file_mode::read:
        dw1 = boost::detail::winapi::GENERIC_READ_;
        dw2 = boost::detail::winapi::OPEN_EXISTING_;
        dw3 = 0x10000000; // FILE_FLAG_RANDOM_ACCESS
        break;

    case file_mode::scan:           
        dw1 = boost::detail::winapi::GENERIC_READ_;
        dw2 = boost::detail::winapi::OPEN_EXISTING_;
        dw3 = 0x08000000; // FILE_FLAG_SEQUENTIAL_SCAN
        break;

    case file_mode::write:          
        dw1 = boost::detail::winapi::GENERIC_READ_ |
              boost::detail::winapi::GENERIC_WRITE_;
        dw2 = boost::detail::winapi::CREATE_ALWAYS_;
        dw3 = 0x10000000; // FILE_FLAG_RANDOM_ACCESS
        break;

    case file_mode::write_new:      
        dw1 = boost::detail::winapi::GENERIC_READ_ |
              boost::detail::winapi::GENERIC_WRITE_;
        dw2 = boost::detail::winapi::CREATE_NEW_;
        dw3 = 0x10000000; // FILE_FLAG_RANDOM_ACCESS
        break;

    case file_mode::write_existing: 
        dw1 = boost::detail::winapi::GENERIC_READ_ |
              boost::detail::winapi::GENERIC_WRITE_;
        dw2 = boost::detail::winapi::OPEN_EXISTING_;
        dw3 = 0x10000000; // FILE_FLAG_RANDOM_ACCESS
        break;

    case file_mode::append:         
        dw1 = boost::detail::winapi::GENERIC_READ_ |
              boost::detail::winapi::GENERIC_WRITE_;
        dw2 = boost::detail::winapi::CREATE_ALWAYS_;
        dw3 = 0x08000000; // FILE_FLAG_SEQUENTIAL_SCAN
        break;

    case file_mode::append_new:     
        dw1 = boost::detail::winapi::GENERIC_READ_ |
              boost::detail::winapi::GENERIC_WRITE_;
        dw2 = boost::detail::winapi::CREATE_NEW_;
        dw3 = 0x08000000; // FILE_FLAG_SEQUENTIAL_SCAN
        break;

    case file_mode::append_existing:
        dw1 = boost::detail::winapi::GENERIC_READ_ |
              boost::detail::winapi::GENERIC_WRITE_;
        dw2 = boost::detail::winapi::OPEN_EXISTING_;
        dw3 = 0x08000000; // FILE_FLAG_SEQUENTIAL_SCAN
        break;
    }
    h_ = ::CreateFileA(
        path,
        dw1,
        0,
        NULL,
        dw2,
        dw3,
        NULL);
    if(h_ == boost::detail::winapi::INVALID_HANDLE_VALUE_)
        ec.assign(boost::detail::winapi::GetLastError(),
            system_category());
    else
        ec.assign(0, ec.category());
}

inline
std::uint64_t
file_win32::
size(error_code& ec) const
{
    if(h_ == boost::detail::winapi::INVALID_HANDLE_VALUE_)
    {
        ec.assign(errc::invalid_argument, generic_category());
        return 0;
    }
    boost::detail::winapi::LARGE_INTEGER_ fileSize;
    if(! boost::detail::winapi::GetFileSizeEx(h_, &fileSize))
    {
        ec.assign(boost::detail::winapi::GetLastError(),
            system_category());
        return 0;
    }
    ec.assign(0, ec.category());
    return fileSize.QuadPart;
}

inline
std::uint64_t
file_win32::
pos(error_code& ec)
{
    if(h_ == boost::detail::winapi::INVALID_HANDLE_VALUE_)
    {
        ec.assign(errc::invalid_argument, generic_category());
        return 0;
    }
    boost::detail::winapi::LARGE_INTEGER_ in;
    boost::detail::winapi::LARGE_INTEGER_ out;
    in.QuadPart = 0;
    if(! detail::set_file_pointer_ex(h_, in, &out,
        boost::detail::winapi::FILE_CURRENT_))
    {
        ec.assign(boost::detail::winapi::GetLastError(),
            system_category());
        return 0;
    }
    ec.assign(0, ec.category());
    return out.QuadPart;
}

inline
void
file_win32::
seek(std::uint64_t offset, error_code& ec)
{
    if(h_ == boost::detail::winapi::INVALID_HANDLE_VALUE_)
    {
        ec.assign(errc::invalid_argument, generic_category());
        return;
    }
    boost::detail::winapi::LARGE_INTEGER_ in;
    in.QuadPart = offset;
    if(! detail::set_file_pointer_ex(h_, in, 0,
        boost::detail::winapi::FILE_BEGIN_))
    {
        ec.assign(boost::detail::winapi::GetLastError(),
            system_category());
        return;
    }
    ec.assign(0, ec.category());
}

inline
std::size_t
file_win32::
read(void* buffer, std::size_t n, error_code& ec)
{
    if(h_ == boost::detail::winapi::INVALID_HANDLE_VALUE_)
    {
        ec.assign(errc::invalid_argument, generic_category());
        return 0;
    }
    std::size_t nread = 0;
    while(n > 0)
    {
        boost::detail::winapi::DWORD_ amount;
        if(n > (std::numeric_limits<
                boost::detail::winapi::DWORD_>::max)())
            amount = (std::numeric_limits<
                boost::detail::winapi::DWORD_>::max)();
        else
            amount = static_cast<
                boost::detail::winapi::DWORD_>(n);
        boost::detail::winapi::DWORD_ bytesRead;
        if(! ::ReadFile(h_, buffer, amount, &bytesRead, 0))
        {
            auto const dwError = ::GetLastError();
            if(dwError != boost::detail::winapi::ERROR_HANDLE_EOF_)
                ec.assign(::GetLastError(), system_category());
            else
                ec.assign(0, ec.category());
            return nread;
        }
        if(bytesRead == 0)
            return nread;
        n -= bytesRead;
        nread += bytesRead;
        buffer = reinterpret_cast<char*>(buffer) + bytesRead;
    }
    ec.assign(0, ec.category());
    return nread;
}

inline
std::size_t
file_win32::
write(void const* buffer, std::size_t n, error_code& ec)
{
    if(h_ == boost::detail::winapi::INVALID_HANDLE_VALUE_)
    {
        ec.assign(errc::invalid_argument, generic_category());
        return 0;
    }
    std::size_t nwritten = 0;
    while(n > 0)
    {
        boost::detail::winapi::DWORD_ amount;
        if(n > (std::numeric_limits<
                boost::detail::winapi::DWORD_>::max)())
            amount = (std::numeric_limits<
                boost::detail::winapi::DWORD_>::max)();
        else
            amount = static_cast<
                boost::detail::winapi::DWORD_>(n);
        boost::detail::winapi::DWORD_ bytesWritten;
        if(! ::WriteFile(h_, buffer, amount, &bytesWritten, 0))
        {
            auto const dwError = ::GetLastError();
            if(dwError != boost::detail::winapi::ERROR_HANDLE_EOF_)
                ec.assign(::GetLastError(), system_category());
            else
                ec.assign(0, ec.category());
            return nwritten;
        }
        if(bytesWritten == 0)
            return nwritten;
        n -= bytesWritten;
        nwritten += bytesWritten;
        buffer = reinterpret_cast<char const*>(buffer) + bytesWritten;
    }
    ec.assign(0, ec.category());
    return nwritten;
}

} // beast

#endif
