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

#ifndef BEAST_WSPROTO_INVOKABLE_H_INCLUDED
#define BEAST_WSPROTO_INVOKABLE_H_INCLUDED

#include <array>
#include <cassert>
#include <memory>
#include <utility>

namespace beast {
namespace wsproto {
namespace detail {

// "Parks" a composed operation, to invoke later
//
class invokable
{
    struct base
    {
        virtual ~base() = default;
        virtual void move(void* p) = 0;
        virtual void operator()() = 0;
    };

    template<class F>
    struct holder : base
    {
        F f;

        holder(holder&&) = default;
        holder(holder const&) = default;

        template<class U>
        explicit
        holder(U&& u)
            : f(std::forward<U>(u))
        {
        }

        void
        move(void* p) override
        {
            ::new(p) holder(std::move(*this));
        }

        void
        operator()() override
        {
            F f_(std::move(f));
            this->~holder();
            // invocation of f_() can
            // assign a new invokable.
            f_();
        }
    };

    struct exemplar
    {
        std::shared_ptr<int> _;
        void operator()(){}
    };

    using buf_type = std::uint8_t[
        sizeof(holder<exemplar>)];

    bool b_ = false;
    buf_type buf_;

public:
    invokable()
    {
    }
    invokable(invokable const&) = delete;
    invokable& operator=(invokable&&) = delete;
    invokable& operator=(invokable const&) = delete;

#ifndef NDEBUG
    ~invokable()
    {
        // Engaged invokables must be invoked before
        // destruction otherwise the io_service
        // invariants are broken w.r.t completions.
        assert(! b_);
    }
#endif

    invokable(invokable&& other)
        : b_(other.b_)
    {
        if(other.b_)
            other.get().move(buf_);
    }

    template<class F>
    void
    emplace(F&& f);

    void
    maybe_invoke()
    {
        if(b_)
        {
            b_ = false;
            get()();
        }
    }

private:
    base&
    get()
    {
        return *reinterpret_cast<base*>(buf_);
    }
};

template<class F>
void
invokable::emplace(F&& f)
{
    static_assert(sizeof(buf_type) >= sizeof(holder<F>),
        "buffer too small");
    assert(! b_);
    ::new(buf_) holder<F>(std::forward<F>(f));
    b_ = true;
}

} // detail
} // wsproto
} // beast

#endif
