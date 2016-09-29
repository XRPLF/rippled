//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_DETAIL_WIN32_FILE_HPP
#define NUDB_DETAIL_WIN32_FILE_HPP

#include <nudb/file.hpp>
#include <nudb/error.hpp>
#include <cstddef>
#include <cstdint>
#include <string>

#ifndef NUDB_WIN32_FILE
# ifdef _MSC_VER
#  define NUDB_WIN32_FILE 1
# else
#  define NUDB_WIN32_FILE 0
# endif
#endif

#if NUDB_WIN32_FILE
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

#if NUDB_WIN32_FILE

namespace nudb {

/** A descriptor to a Win32 file.

    This class provides a Win32 implementation of the @b File
    concept.
*/
class win32_file
{
    HANDLE hf_ = INVALID_HANDLE_VALUE;

public:
    /// Constructor
    win32_file() = default;

    /// Copy constructor (disallowed)
    win32_file(win32_file const&) = delete;

    // Copy assignment (disallowed)
    win32_file& operator=(win32_file const&) = delete;

    /** Destructor.

        If open, the file is closed.
    */
    ~win32_file();

    /** Move constructor.

        @note The state of the moved-from object is as if default constructed.
    */
    win32_file(win32_file&&);

    /** Move assignment.

        @note The state of the moved-from object is as if default constructed.
    */
    win32_file&
    operator=(win32_file&& other);

    /// Returns `true` if the file is open.
    bool
    is_open() const
    {
        return hf_ != INVALID_HANDLE_VALUE;
    }

    /// Close the file if it is open.
    void
    close();

    /** Create a new file.

        After the file is created, it is opened as if by `open(mode, path, ec)`.

        @par Requirements

        The file must not already exist, or else `errc::file_exists`
        is returned.

        @param mode The open mode, which must be a valid @ref file_mode.

        @param path The path of the file to create.

        @param ec Set to the error, if any occurred.
    */
    void
    create(file_mode mode, path_type const& path, error_code& ec);

    /** Open a file.

        @par Requirements

        The file must not already be open.

        @param mode The open mode, which must be a valid @ref file_mode.

        @param path The path of the file to open.

        @param ec Set to the error, if any occurred.
    */
    void
    open(file_mode mode, path_type const& path, error_code& ec);

    /** Remove a file from the file system.

        It is not an error to attempt to erase a file that does not exist.

        @param path The path of the file to remove.

        @param ec Set to the error, if any occurred.
    */
    static
    void
    erase(path_type const& path, error_code& ec);

    /** Return the size of the file.

        @par Requirements

        The file must be open.

        @param ec Set to the error, if any occurred.

        @return The size of the file, in bytes.
    */
    std::uint64_t
    size(error_code& ec) const;

    /** Read data from a location in the file.

        @par Requirements

        The file must be open.

        @param offset The position in the file to read from,
        expressed as a byte offset from the beginning.

        @param buffer The location to store the data.

        @param bytes The number of bytes to read.

        @param ec Set to the error, if any occurred.
    */
    void
    read(std::uint64_t offset,
        void* buffer, std::size_t bytes, error_code& ec);

    /** Write data to a location in the file.

        @par Requirements

        The file must be open with a mode allowing writes.

        @param offset The position in the file to write from,
        expressed as a byte offset from the beginning.

        @param buffer The data the write.

        @param bytes The number of bytes to write.

        @param ec Set to the error, if any occurred.
    */
    void
    write(std::uint64_t offset,
        void const* buffer, std::size_t bytes, error_code& ec);

    /** Perform a low level file synchronization.

        @par Requirements

        The file must be open with a mode allowing writes.

        @param ec Set to the error, if any occurred.
    */
    void
    sync(error_code& ec);

    /** Truncate the file at a specific size.

        @par Requirements

        The file must be open with a mode allowing writes.

        @param length The new file size.

        @param ec Set to the error, if any occurred.
    */
    void
    trunc(std::uint64_t length, error_code& ec);

private:
    static
    void
    err(DWORD dwError, error_code& ec)
    {
        ec = error_code{static_cast<int>(dwError), system_category()};
    }

    static
    void
    last_err(error_code& ec)
    {
        err(::GetLastError(), ec);
    }

    static
    std::pair<DWORD, DWORD>
    flags(file_mode mode);
};

} // nudb

#include <nudb/impl/win32_file.ipp>

#endif

#endif
