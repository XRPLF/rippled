//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_NUDB_DETAIL_WIN32_FILE_H_INCLUDED
#define BEAST_NUDB_DETAIL_WIN32_FILE_H_INCLUDED

#include <beast/nudb/common.h>
#include <cassert>
#include <string>

#ifndef BEAST_NUDB_WIN32_FILE
# ifdef _MSC_VER
#  define BEAST_NUDB_WIN32_FILE 1
# else
#  define BEAST_NUDB_WIN32_FILE 0
# endif
#endif

#if BEAST_NUDB_WIN32_FILE
#pragma push_macro("NOMINMAX")
#pragma push_macro("UNICODE")
#pragma push_macro("STRICT")
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# ifndef UNICODE
#  define UNICODE
# endif
# ifndef STRICT
#  define STRICT
# endif
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# include <Windows.h>
#pragma pop_macro("STRICT")
#pragma pop_macro("UNICODE")
#pragma pop_macro("NOMINMAX")
#endif

namespace beast {
namespace nudb {

#if BEAST_NUDB_WIN32_FILE

namespace detail {

// Win32 error code
class file_win32_error
    : public file_error
{
public:
    explicit
    file_win32_error (char const* m,
            DWORD dwError = ::GetLastError())
        : file_error (std::string("nudb: ") + m +
            ", " + text(dwError))
    {
    }

    explicit
    file_win32_error (std::string const& m,
            DWORD dwError = ::GetLastError())
        : file_error (std::string("nudb: ") + m +
            ", " + text(dwError))
    {
    }

private:
    template <class = void>
    static
    std::string
    text (DWORD dwError);
};

template <class>
std::string
file_win32_error::text (DWORD dwError)
{
    LPSTR buf = nullptr;
    size_t const size = FormatMessageA (
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dwError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&buf,
        0,
        NULL);
    std::string s;
    if (size)
    {
        s.append(buf, size);
        LocalFree (buf);
    }
    else
    {
        s = "error " + std::to_string(dwError);
    }
    return s;
}

//------------------------------------------------------------------------------

template <class = void>
class win32_file
{
private:
    HANDLE hf_ = INVALID_HANDLE_VALUE;

public:
    win32_file() = default;
    win32_file (win32_file const&) = delete;
    win32_file& operator= (win32_file const&) = delete;

    ~win32_file();

    win32_file (win32_file&&);

    win32_file&
    operator= (win32_file&& other);

    bool
    is_open() const
    {
        return hf_ != INVALID_HANDLE_VALUE;
    }

    void
    close();

    //  Returns:
    //      `false` if the file already exists
    //      `true` on success, else throws
    //
    bool
    create (file_mode mode, std::string const& path);

    //  Returns:
    //      `false` if the file doesnt exist
    //      `true` on success, else throws
    //
    bool
    open (file_mode mode, std::string const& path);

    //  Effects:
    //      Removes the file from the file system.
    //
    //  Throws:
    //      Throws is an error occurs.
    //
    //  Returns:
    //      `true` if the file was erased
    //      `false` if the file was not present
    //
    static
    bool
    erase (path_type const& path);

    //  Returns:
    //      Current file size in bytes measured by operating system
    //  Requires:
    //      is_open() == true
    //
    std::size_t
    actual_size() const;

    void
    read (std::size_t offset,
        void* buffer, std::size_t bytes);

    void
    write (std::size_t offset,
        void const* buffer, std::size_t bytes);

    void
    sync();

    void
    trunc (std::size_t length);

private:
    static
    std::pair<DWORD, DWORD>
    flags (file_mode mode);
};

template <class _>
win32_file<_>::~win32_file()
{
    close();
}

template <class _>
win32_file<_>::win32_file (win32_file&& other)
    : hf_ (other.hf_)
{
    other.hf_ = INVALID_HANDLE_VALUE;
}

template <class _>
win32_file<_>&
win32_file<_>::operator= (win32_file&& other)
{
    if (&other == this)
        return *this;
    close();
    hf_ = other.hf_;
    other.hf_ = INVALID_HANDLE_VALUE;
    return *this;
}

template <class _>
void
win32_file<_>::close()
{
    if (hf_ != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(hf_);
        hf_ = INVALID_HANDLE_VALUE;
    }
}

template <class _>
bool
win32_file<_>::create (file_mode mode,
    std::string const& path)
{
    assert(! is_open());
    auto const f = flags(mode);
    hf_ = ::CreateFileA (path.c_str(),
        f.first,
        0,
        NULL,
        CREATE_NEW,
        f.second,
        NULL);
    if (hf_ == INVALID_HANDLE_VALUE)
    {
        DWORD const dwError = ::GetLastError();
        if (dwError != ERROR_FILE_EXISTS)
            throw file_win32_error(
                "create file", dwError);
        return false;
    }
    return true;
}

template <class _>
bool
win32_file<_>::open (file_mode mode,
    std::string const& path)
{
    assert(! is_open());
    auto const f = flags(mode);
    hf_ = ::CreateFileA (path.c_str(),
        f.first,
        0,
        NULL,
        OPEN_EXISTING,
        f.second,
        NULL);
    if (hf_ == INVALID_HANDLE_VALUE)
    {
        DWORD const dwError = ::GetLastError();
        if (dwError != ERROR_FILE_NOT_FOUND &&
            dwError != ERROR_PATH_NOT_FOUND)
            throw file_win32_error(
                "open file", dwError);
        return false;
    }
    return true;
}

template <class _>
bool
win32_file<_>::erase (path_type const& path)
{
    BOOL const bSuccess =
        ::DeleteFileA(path.c_str());
    if (! bSuccess)
    {
        DWORD dwError = ::GetLastError();
        if (dwError != ERROR_FILE_NOT_FOUND &&
            dwError != ERROR_PATH_NOT_FOUND)
            throw file_win32_error(
                "erase file");
        return false;
    }
    return true;
}

// Return: Current file size in bytes measured by operating system
template <class _>
std::size_t
win32_file<_>::actual_size() const
{
    assert(is_open());
    LARGE_INTEGER fileSize;
    if (! ::GetFileSizeEx(hf_, &fileSize))
        throw file_win32_error(
            "size file");
    return static_cast<std::size_t>(fileSize.QuadPart);
}

template <class _>
void
win32_file<_>::read (std::size_t offset,
    void* buffer, std::size_t bytes)
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
        BOOL const bSuccess = ::ReadFile(
            hf_, buffer, bytes, &bytesRead, &ov);
        if (! bSuccess)
        {
            DWORD const dwError = ::GetLastError();
            if (dwError != ERROR_HANDLE_EOF)
                throw file_win32_error(
                    "read file", dwError);
            throw file_short_read_error();
        }
        if (bytesRead == 0)
            throw file_short_read_error();
        offset += bytesRead;
        bytes -= bytesRead;
        buffer = reinterpret_cast<char*>(
            buffer) + bytesRead;
    }
}

template <class _>
void
win32_file<_>::write (std::size_t offset,
    void const* buffer, std::size_t bytes)
{
    while(bytes > 0)
    {
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        OVERLAPPED ov;
        ov.Offset = li.LowPart;
        ov.OffsetHigh = li.HighPart;
        ov.hEvent = NULL;
        DWORD bytesWritten;
        BOOL const bSuccess = ::WriteFile(
            hf_, buffer, bytes, &bytesWritten, &ov);
        if (! bSuccess)
            throw file_win32_error(
                "write file");
        if (bytesWritten == 0)
            throw file_short_write_error();
        offset += bytesWritten;
        bytes -= bytesWritten;
        buffer = reinterpret_cast<
            char const*>(buffer) +
                bytesWritten;
    }
}

template <class _>
void
win32_file<_>::sync()
{
    BOOL const bSuccess =
        ::FlushFileBuffers(hf_);
    if (! bSuccess)
        throw file_win32_error(
            "sync file");
}

template <class _>
void
win32_file<_>::trunc (std::size_t length)
{
    LARGE_INTEGER li;
    li.QuadPart = length;
    BOOL bSuccess;
    bSuccess = ::SetFilePointerEx(
        hf_, li, NULL, FILE_BEGIN);
    if (bSuccess)
        bSuccess = SetEndOfFile(hf_);
    if (! bSuccess)
        throw file_win32_error(
            "trunc file");
}

template <class _>
std::pair<DWORD, DWORD>
win32_file<_>::flags (file_mode mode)
{
    std::pair<DWORD, DWORD> result(0, 0);
    switch (mode)
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

} // detail

using win32_file = detail::win32_file<>;

#endif

} // nudb
} // detail

#endif
