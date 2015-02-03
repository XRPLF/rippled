//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_NUDB_FAIL_FILE_H_INCLUDED
#define BEAST_NUDB_FAIL_FILE_H_INCLUDED

#include <beast/nudb/common.h>
#include <atomic>
#include <cstddef>
#include <string>
#include <utility>

namespace beast {
namespace nudb {

/** Thrown when a test failure mode occurs. */
struct fail_error : std::exception
{
    char const*
    what() const noexcept override
    {
        return "test failure";
    }
};

/** Countdown to test failure modue. */
class fail_counter
{
private:
    std::size_t target_;
    std::atomic<std::size_t> count_;

public:
    fail_counter (fail_counter const&) = delete;
    fail_counter& operator= (fail_counter const&) = delete;

    explicit
    fail_counter (std::size_t target = 0)
    {
        reset (target);
    }

    /** Reset the counter to fail at the nth step, or 0 for no failure. */
    void
    reset (std::size_t n = 0)
    {
        target_ = n;
        count_.store(0);
    }

    bool
    fail()
    {
        return target_ && (++count_ >= target_);
    }
};

/** Wrapper to simulate file system failures. */
template <class File>
class fail_file
{
private:
    File f_;
    fail_counter* c_ = nullptr;

public:
    fail_file() = default;
    fail_file (fail_file const&) = delete;
    fail_file& operator= (fail_file const&) = delete;
    ~fail_file() = default;

    fail_file (fail_file&&);

    fail_file&
    operator= (fail_file&& other);

    explicit
    fail_file (fail_counter& c);

    bool
    is_open() const
    {
        return f_.is_open();
    }

    path_type const&
    path() const
    {
        return f_.path();
    }

    std::size_t
    actual_size() const
    {
        return f_.actual_size();
    }

    void
    close()
    {
        f_.close();
    }

    bool
    create (file_mode mode,
        path_type const& path)
    {
        return f_.create(mode, path);
    }

    bool
    open (file_mode mode,
        path_type const& path)
    {
        return f_.open(mode, path);
    }

    static
    void
    erase (path_type const& path)
    {
        File::erase(path);
    }

    void
    read (std::size_t offset,
        void* buffer, std::size_t bytes)
    {
        f_.read(offset, buffer, bytes);
    }

    void
    write (std::size_t offset,
        void const* buffer, std::size_t bytes);

    void
    sync();

    void
    trunc (std::size_t length);

private:
    bool
    fail();

    void
    do_fail();
};

template <class File>
fail_file<File>::fail_file (fail_file&& other)
    : f_ (std::move(other.f_))
    , c_ (other.c_)
{
    other.c_ = nullptr;
}

template <class File>
fail_file<File>&
fail_file<File>::operator= (fail_file&& other)
{
    f_ = std::move(other.f_);
    c_ = other.c_;
    other.c_ = nullptr;
    return *this;
}

template <class File>
fail_file<File>::fail_file (fail_counter& c)
    : c_ (&c)
{
}

template <class File>
void
fail_file<File>::write (std::size_t offset,
    void const* buffer, std::size_t bytes)
{
    if (fail())
        do_fail();
    if (fail())
    {
        f_.write(offset, buffer, (bytes + 1) / 2);
        do_fail();
    }
    f_.write(offset, buffer, bytes);
}

template <class File>
void
fail_file<File>::sync()
{
    if (fail())
        do_fail();
    // We don't need a real sync for
    // testing, it just slows things down.
    //f_.sync();
}

template <class File>
void
fail_file<File>::trunc (std::size_t length)
{
    if (fail())
        do_fail();
    f_.trunc(length);
}

template <class File>
bool
fail_file<File>::fail()
{
    if (c_)
        return c_->fail();
    return false;
}

template <class File>
void
fail_file<File>::do_fail()
{
    throw fail_error();
}

}
}

#endif

