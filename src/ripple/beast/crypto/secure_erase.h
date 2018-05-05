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

#ifndef BEAST_CRYPTO_SECURE_ERASE_H_INCLUDED
#define BEAST_CRYPTO_SECURE_ERASE_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <new>

namespace beast {

namespace detail {

class secure_erase_impl
{
private:
    struct base
    {
        virtual void operator()(
            void* dest, std::size_t bytes) const = 0;
        virtual ~base() = default;
        base() = default;
        base(base const&) = delete;
        base& operator=(base const&) = delete;
    };

    struct impl : base
    {
        void operator()(
            void* dest, std::size_t bytes) const override
        {
            char volatile* volatile p =
                const_cast<volatile char*>(
                    reinterpret_cast<char*>(dest));
            if (bytes == 0)
                return;
            do
            {
                *p = 0;
            }
            while(*p++ == 0 && --bytes);
        }
    };

    char buf_[sizeof(impl)];
    base& erase_;

public:
    secure_erase_impl()
        : erase_(*new(buf_) impl)
    {
    }

    void operator()(
        void* dest, std::size_t bytes) const
    {
        return erase_(dest, bytes);
    }
};

}

/** Guaranteed to fill memory with zeroes */
template <class = void>
void
secure_erase (void* dest, std::size_t bytes)
{
    static detail::secure_erase_impl const erase;
    erase(dest, bytes);
}

}

#endif
