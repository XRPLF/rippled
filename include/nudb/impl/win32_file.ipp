//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_IMPL_WIN32_FILE_IPP
#define NUDB_IMPL_WIN32_FILE_IPP

#include <boost/assert.hpp>

namespace nudb {

inline
win32_file::
~win32_file()
{
    close();
}

inline
win32_file::
win32_file(win32_file&& other)
    : hf_(other.hf_)
{
    other.hf_ = INVALID_HANDLE_VALUE;
}

inline
win32_file&
win32_file::
operator=(win32_file&& other)
{
    if(&other == this)
        return *this;
    close();
    hf_ = other.hf_;
    other.hf_ = INVALID_HANDLE_VALUE;
    return *this;
}

inline
void
win32_file::
close()
{
    if(hf_ != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(hf_);
        hf_ = INVALID_HANDLE_VALUE;
    }
}

inline
void
win32_file::
create(file_mode mode, path_type const& path, error_code& ec)
{
    BOOST_ASSERT(! is_open());
    auto const f = flags(mode);
    hf_ = ::CreateFileA(path.c_str(),
        f.first,
        0,
        NULL,
        CREATE_NEW,
        f.second,
        NULL);
    if(hf_ == INVALID_HANDLE_VALUE)
        return last_err(ec);
}

inline
void
win32_file::
open(file_mode mode, path_type const& path, error_code& ec)
{
    BOOST_ASSERT(! is_open());
    auto const f = flags(mode);
    hf_ = ::CreateFileA(path.c_str(),
        f.first,
        0,
        NULL,
        OPEN_EXISTING,
        f.second,
        NULL);
    if(hf_ == INVALID_HANDLE_VALUE)
        return last_err(ec);
}

inline
void
win32_file::
erase(path_type const& path, error_code& ec)
{
    BOOL const bSuccess =
        ::DeleteFileA(path.c_str());
    if(! bSuccess)
        return last_err(ec);
}

inline
std::uint64_t
win32_file::
size(error_code& ec) const
{
    BOOST_ASSERT(is_open());
    LARGE_INTEGER fileSize;
    if(! ::GetFileSizeEx(hf_, &fileSize))
    {
        last_err(ec);
        return 0;
    }
    return fileSize.QuadPart;
}

inline
void
win32_file::
read(std::uint64_t offset, void* buffer, std::size_t bytes, error_code& ec)
{
    while(bytes > 0)
    {
        DWORD bytesRead;
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        OVERLAPPED ov;
        ov.Offset = li.LowPart;
        ov.OffsetHigh = li.HighPart;
        ov.hEvent = NULL;
        DWORD amount;
        if(bytes > std::numeric_limits<DWORD>::max())
            amount = std::numeric_limits<DWORD>::max();
        else
            amount = static_cast<DWORD>(bytes);
        BOOL const bSuccess = ::ReadFile(
            hf_, buffer, amount, &bytesRead, &ov);
        if(! bSuccess)
        {
            DWORD const dwError = ::GetLastError();
            if(dwError != ERROR_HANDLE_EOF)
                return err(dwError, ec);
            ec = make_error_code(error::short_read);
            return;
        }
        if(bytesRead == 0)
        {
            ec = make_error_code(error::short_read);
            return;
        }
        offset += bytesRead;
        bytes -= bytesRead;
        buffer = reinterpret_cast<char*>(
            buffer) + bytesRead;
    }
}

inline
void
win32_file::
write(std::uint64_t offset,
    void const* buffer, std::size_t bytes, error_code& ec)
{
    while(bytes > 0)
    {
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        OVERLAPPED ov;
        ov.Offset = li.LowPart;
        ov.OffsetHigh = li.HighPart;
        ov.hEvent = NULL;
        DWORD amount;
        if(bytes > std::numeric_limits<DWORD>::max())
            amount = std::numeric_limits<DWORD>::max();
        else
            amount = static_cast<DWORD>(bytes);
        DWORD bytesWritten;
        BOOL const bSuccess = ::WriteFile(
            hf_, buffer, amount, &bytesWritten, &ov);
        if(! bSuccess)
            return last_err(ec);
        if(bytesWritten == 0)
        {
            ec = error_code{errc::no_space_on_device,
                generic_category()};;
            return;
        }
        offset += bytesWritten;
        bytes -= bytesWritten;
        buffer = reinterpret_cast<char const*>(
            buffer) + bytesWritten;
    }
}

inline
void
win32_file::
sync(error_code& ec)
{
    if(! ::FlushFileBuffers(hf_))
        return last_err(ec);
}

inline
void
win32_file::
trunc(std::uint64_t length, error_code& ec)
{
    LARGE_INTEGER li;
    li.QuadPart = length;
    BOOL bSuccess;
    bSuccess = ::SetFilePointerEx(
        hf_, li, NULL, FILE_BEGIN);
    if(bSuccess)
        bSuccess = ::SetEndOfFile(hf_);
    if(! bSuccess)
        return last_err(ec);
}

inline
std::pair<DWORD, DWORD>
win32_file::
flags(file_mode mode)
{
    std::pair<DWORD, DWORD> result{0, 0};
    switch(mode)
    {
    case file_mode::scan:
        result.first =
            GENERIC_READ;
        result.second =
            FILE_FLAG_SEQUENTIAL_SCAN;
        break;

    case file_mode::read:
        result.first =
            GENERIC_READ;
        result.second =
            FILE_FLAG_RANDOM_ACCESS;
        break;

    case file_mode::append:
        result.first =
            GENERIC_READ | GENERIC_WRITE;
        result.second =
            FILE_FLAG_RANDOM_ACCESS
            //| FILE_FLAG_NO_BUFFERING
            //| FILE_FLAG_WRITE_THROUGH
            ;
        break;

    case file_mode::write:
        result.first =
            GENERIC_READ | GENERIC_WRITE;
        result.second =
            FILE_FLAG_RANDOM_ACCESS;
        break;
    }
    return result;
}

} // nudb

#endif
