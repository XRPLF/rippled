//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NUDB_IMPL_POSIX_FILE_IPP
#define NUDB_IMPL_POSIX_FILE_IPP

#include <boost/assert.hpp>
#include <limits.h>

namespace nudb {

inline
posix_file::
~posix_file()
{
    close();
}

inline
posix_file::
posix_file(posix_file &&other)
    : fd_(other.fd_)
{
    other.fd_ = -1;
}

inline
posix_file&
posix_file::
operator=(posix_file&& other)
{
    if(&other == this)
        return *this;
    close();
    fd_ = other.fd_;
    other.fd_ = -1;
    return *this;
}

inline
void
posix_file::
close()
{
    if(fd_ != -1)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

inline
void
posix_file::
create(file_mode mode, path_type const& path, error_code& ec)
{
    auto const result = flags(mode);
    BOOST_ASSERT(! is_open());
    fd_ = ::open(path.c_str(), result.first);
    if(fd_ != -1)
    {
        ::close(fd_);
        fd_ = -1;
        ec = error_code{errc::file_exists, generic_category()};
        return ;
    }
    int errnum = errno;
    if(errnum != ENOENT)
        return err(errnum, ec);
    fd_ = ::open(path.c_str(), result.first | O_CREAT, 0644);
    if(fd_ == -1)
        return last_err(ec);
#ifndef __APPLE__
    if(::posix_fadvise(fd_, 0, 0, result.second) != 0)
        return last_err(ec);
#endif
}

inline
void
posix_file::
open(file_mode mode, path_type const& path, error_code& ec)
{
    BOOST_ASSERT(! is_open());
    auto const result = flags(mode);
    fd_ = ::open(path.c_str(), result.first);
    if(fd_ == -1)
        return last_err(ec);
#ifndef __APPLE__
    if(::posix_fadvise(fd_, 0, 0, result.second) != 0)
        return last_err(ec);
#endif
}

inline
void
posix_file::
erase(path_type const& path, error_code& ec)
{
    if(::unlink(path.c_str()) != 0)
    {
        int const ev = errno;
        return err(ev, ec);
    }
}

inline
std::uint64_t
posix_file::
size(error_code& ec) const
{
    static_assert(sizeof(stat::st_size) == sizeof(std::uint64_t), "");
    struct stat st;
    if(::fstat(fd_, &st) != 0)
    {
        last_err(ec);
        return 0;
    }
    return st.st_size;
}
inline
void
posix_file::
read(std::uint64_t offset,
     void* buffer, std::size_t bytes, error_code& ec)
{
    static_assert(sizeof(off_t) >= sizeof(offset), "");
    while(bytes > 0)
    {
        auto const amount = static_cast<ssize_t>(
            std::min(bytes, static_cast<std::size_t>(SSIZE_MAX)));
        auto const n = ::pread(fd_, buffer, amount, offset);
        if(n == -1)
        {
            auto const ev = errno;
            if(ev == EINTR)
                continue;
            return err(ev, ec);
        }
        if(n == 0)
        {
            ec = error::short_read;
            return;
        }
        offset += n;
        bytes -= n;
        buffer = reinterpret_cast<char*>(buffer) + n;
    }
}

inline
void
posix_file::
write(std::uint64_t offset,
      void const* buffer, std::size_t bytes, error_code& ec)
{
    static_assert(sizeof(off_t) >= sizeof(offset), "");
    while(bytes > 0)
    {
        auto const amount = static_cast<ssize_t>(
            std::min(bytes, static_cast<std::size_t>(SSIZE_MAX)));
        auto const n = ::pwrite(fd_, buffer, amount, offset);
        if(n == -1)
        {
            auto const ev = errno;
            if(ev == EINTR)
                continue;
            return err(ev, ec);
        }
        offset += n;
        bytes -= n;
        buffer = reinterpret_cast<char const*>(buffer) + n;
    }
}

inline
void
posix_file::
sync(error_code& ec)
{
    for(;;)
    {
        if(::fsync(fd_) == 0)
            break;
        auto const ev = errno;
        if(ev == EINTR)
            continue;
        return err(ev, ec);
    }
}

inline
void
posix_file::
trunc(std::uint64_t length, error_code& ec)
{
    for(;;)
    {
        if(::ftruncate(fd_, length) == 0)
            break;
        auto const ev = errno;
        if(ev == EINTR)
            continue;
        return err(ev, ec);
    }
}

inline
std::pair<int, int>
posix_file::
flags(file_mode mode)
{
    std::pair<int, int> result;
    switch(mode)
    {
    case file_mode::scan:
        result.first =
            O_RDONLY;
    #ifndef __APPLE__
        result.second =
            POSIX_FADV_SEQUENTIAL;
    #endif
        break;
    case file_mode::read:
        result.first =
            O_RDONLY;
    #ifndef __APPLE__
        result.second =
            POSIX_FADV_RANDOM;
    #endif
        break;
    case file_mode::append:
        result.first =
            O_RDWR |
            O_APPEND;
    #ifndef __APPLE__
        result.second =
            POSIX_FADV_RANDOM;
    #endif
        break;
    case file_mode::write:
        result.first =
            O_RDWR;
    #ifndef __APPLE__
        result.second =
            POSIX_FADV_NORMAL;
    #endif
        break;
    }
    return result;
}

} // nudb

#endif
