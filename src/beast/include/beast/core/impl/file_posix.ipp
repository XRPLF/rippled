//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_CORE_IMPL_FILE_POSIX_IPP
#define BEAST_CORE_IMPL_FILE_POSIX_IPP

#include <limits>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

namespace beast {

namespace detail {

inline
int
file_posix_close(int fd)
{
    for(;;)
    {
        if(! ::close(fd))
            break;
        int const ev = errno;
        if(errno != EINTR)
            return ev;
    }
    return 0;
}

} // detail

inline
file_posix::
~file_posix()
{
    if(fd_ != -1)
        detail::file_posix_close(fd_);
}

inline
file_posix::
file_posix(file_posix&& other)
    : fd_(other.fd_)
{
    other.fd_ = -1;
}

inline
file_posix&
file_posix::
operator=(file_posix&& other)
{
    if(&other == this)
        return *this;
    if(fd_ != -1)
        detail::file_posix_close(fd_);
    fd_ = other.fd_;
    other.fd_ = -1;
    return *this;
}

inline
void
file_posix::
native_handle(native_handle_type fd)
{
    if(fd_ != -1)
         detail::file_posix_close(fd_);
    fd_ = fd;
}

inline
void
file_posix::
close(error_code& ec)
{
    if(fd_ != -1)
    {
        auto const ev =
            detail::file_posix_close(fd_);
        if(ev)
            ec.assign(ev, generic_category());
        else
            ec.assign(0, ec.category());
        fd_ = -1;
    }
    else
    {
        ec.assign(0, ec.category());
    }
}

inline
void
file_posix::
open(char const* path, file_mode mode, error_code& ec)
{
    if(fd_ != -1)
    {
        auto const ev =
            detail::file_posix_close(fd_);
        if(ev)
            ec.assign(ev, generic_category());
        else
            ec.assign(0, ec.category());
        fd_ = -1;
    }
    int f = 0;
#ifndef __APPLE__
    int advise = 0;
#endif
    switch(mode)
    {
    default:
    case file_mode::read:
        f = O_RDONLY;
    #ifndef __APPLE__
        advise = POSIX_FADV_RANDOM;
    #endif
        break;
    case file_mode::scan:
        f = O_RDONLY;
    #ifndef __APPLE__
        advise = POSIX_FADV_SEQUENTIAL;
    #endif
        break;

    case file_mode::write:
        f = O_RDWR | O_CREAT | O_TRUNC;
    #ifndef __APPLE__
        advise = POSIX_FADV_RANDOM;
    #endif
        break;

    case file_mode::write_new:      
        f = O_RDWR | O_CREAT | O_EXCL;
    #ifndef __APPLE__
        advise = POSIX_FADV_RANDOM;
    #endif
        break;

    case file_mode::write_existing: 
        f = O_RDWR | O_EXCL;
    #ifndef __APPLE__
        advise = POSIX_FADV_RANDOM;
    #endif
        break;

    case file_mode::append:         
        f = O_RDWR | O_CREAT | O_TRUNC;
    #ifndef __APPLE__
        advise = POSIX_FADV_SEQUENTIAL;
    #endif
        break;

    case file_mode::append_new:     
        f = O_RDWR | O_CREAT | O_EXCL;
    #ifndef __APPLE__
        advise = POSIX_FADV_SEQUENTIAL;
    #endif
        break;

    case file_mode::append_existing:
        f = O_RDWR | O_EXCL;
    #ifndef __APPLE__
        advise = POSIX_FADV_SEQUENTIAL;
    #endif
        break;
    }
    for(;;)
    {
        fd_ = ::open(path, f, 0644);
        if(fd_ != -1)
            break;
        auto const ev = errno;
        if(ev != EINTR)
        {
            ec.assign(ev, generic_category());
            return;
        }
    }
#ifndef __APPLE__
    if(::posix_fadvise(fd_, 0, 0, advise))
    {
        auto const ev = errno;
        detail::file_posix_close(fd_);
        fd_ = -1;
        ec.assign(ev, generic_category());
        return;
    }
#endif
    ec.assign(0, ec.category());
}

inline
std::uint64_t
file_posix::
size(error_code& ec) const
{
    if(fd_ == -1)
    {
        ec.assign(errc::invalid_argument, generic_category());
        return 0;
    }
    struct stat st;
    if(::fstat(fd_, &st) != 0)
    {
        ec.assign(errno, generic_category());
        return 0;
    }
    ec.assign(0, ec.category());
    return st.st_size;
}

inline
std::uint64_t
file_posix::
pos(error_code& ec) const
{
    if(fd_ == -1)
    {
        ec.assign(errc::invalid_argument, generic_category());
        return 0;
    }
    auto const result = ::lseek(fd_, 0, SEEK_CUR);
    if(result == (off_t)-1)
    {
        ec.assign(errno, generic_category());
        return 0;
    }
    ec.assign(0, ec.category());
    return result;
}

inline
void
file_posix::
seek(std::uint64_t offset, error_code& ec)
{
    if(fd_ == -1)
    {
        ec.assign(errc::invalid_argument, generic_category());
        return;
    }
    auto const result = ::lseek(fd_, offset, SEEK_SET);
    if(result == static_cast<off_t>(-1))
    {
        ec.assign(errno, generic_category());
        return;
    }
    ec.assign(0, ec.category());
}

inline
std::size_t
file_posix::
read(void* buffer, std::size_t n, error_code& ec) const
{
    if(fd_ == -1)
    {
        ec.assign(errc::invalid_argument, generic_category());
        return 0;
    }
    std::size_t nread = 0;
    while(n > 0)
    {
        auto const amount = static_cast<ssize_t>((std::min)(
            n, static_cast<std::size_t>(SSIZE_MAX)));
        auto const result = ::read(fd_, buffer, amount);
        if(result == -1)
        {
            auto const ev = errno;
            if(ev == EINTR)
                continue;
            ec.assign(ev, generic_category());
            return nread;
        }
        if(result == 0)
        {
            // short read
            return nread;
        }
        n -= result;
        nread += result;
        buffer = reinterpret_cast<char*>(buffer) + result;
    }
    return nread;
}

inline
std::size_t
file_posix::
write(void const* buffer, std::size_t n, error_code& ec)
{
    if(fd_ == -1)
    {
        ec.assign(errc::invalid_argument, generic_category());
        return 0;
    }
    std::size_t nwritten = 0;
    while(n > 0)
    {
        auto const amount = static_cast<ssize_t>((std::min)(
            n, static_cast<std::size_t>(SSIZE_MAX)));
        auto const result = ::write(fd_, buffer, amount);
        if(result == -1)
        {
            auto const ev = errno;
            if(ev == EINTR)
                continue;
            ec.assign(ev, generic_category());
            return nwritten;
        }
        n -= result;
        nwritten += result;
        buffer = reinterpret_cast<char const*>(buffer) + result;
    }
    return nwritten;
}

} // beast

#endif
