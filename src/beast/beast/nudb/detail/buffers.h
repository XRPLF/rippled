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

#ifndef BEAST_NUDB_BUFFERS_H_INCLUDED
#define BEAST_NUDB_BUFFERS_H_INCLUDED

#include <beast/nudb/detail/config.h>
#include <atomic>
#include <mutex>
#include <new>

namespace beast {
namespace nudb {
namespace detail {

// Thread safe pool of temp buffers,
// to avoid needless calls to malloc.
template <class = void>
class buffers_t
{
private:
    struct element
    {
        element* next;
    };

    std::size_t const block_size_;
    std::mutex m_;
    element* h_ = nullptr;

public:
    class value_type
    {
    private:
        buffers_t& b_;
        element* e_;

    public:
        value_type (value_type const&) = delete;
        value_type& operator= (value_type const&) = delete;

        explicit
        value_type (buffers_t& b)
            : b_ (b)
            , e_ (b.acquire())
        {
        }

        ~value_type()
        {
            b_.release(e_);
        }

        std::uint8_t*
        get() const
        {
            return const_cast <std::uint8_t*>(
                reinterpret_cast<
                    std::uint8_t const*>(e_ + 1));
        }
    };

    explicit
    buffers_t (std::size_t block_size);

    ~buffers_t();

private:
    element*
    acquire();

    void
    release (element* e);
};

template <class _>
buffers_t<_>::buffers_t (std::size_t block_size)
    : block_size_ (block_size)
    , h_ (nullptr)
{
}

template <class _>
buffers_t<_>::~buffers_t()
{
    for (element* e = h_; e;)
    {
        element* const next = e->next;
        e->~element();
        delete[] reinterpret_cast<
            std::uint8_t*>(e);
        e = next;
    }
}

template <class _>
auto
buffers_t<_>::acquire() ->
    element*
{
    {
        std::lock_guard<std::mutex> m(m_);
        element* e = h_;
        if (e)
        {
            h_ = e->next;
            return e;
        }
    }
    return ::new(
        new std::uint8_t[
            sizeof(element) + block_size_]
                ) element;
}

template <class _>
void
buffers_t<_>::release (element* e)
{
    std::lock_guard<std::mutex> m(m_);
    e->next = h_;
    h_ = e;
}

using buffers = buffers_t<>;

} // detail
} // nudb
} // beast

#endif
