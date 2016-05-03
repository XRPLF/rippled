//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_INVOKABLE_HPP
#define BEAST_WEBSOCKET_DETAIL_INVOKABLE_HPP

#include <array>
#include <cassert>
#include <memory>
#include <new>
#include <utility>

namespace beast {
namespace websocket {
namespace detail {

// "Parks" a composed operation, to invoke later
//
class invokable
{
    struct base
    {
        base() = default;
        base(base &&) = default;
        virtual ~base() = default;
        virtual void move(void* p) = 0;
        virtual void operator()() = 0;
    };

    template<class F>
    struct holder : base
    {
        F f;

        holder(holder&&) = default;

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

    using buf_type = char[sizeof(holder<exemplar>)];

    base* base_ = nullptr;
    alignas(holder<exemplar>) buf_type buf_;

public:
#ifndef NDEBUG
    ~invokable()
    {
        // Engaged invokables must be invoked before
        // destruction otherwise the io_service
        // invariants are broken w.r.t completions.
        assert(! base_);
    }
#endif

    invokable() = default;

    invokable(invokable&& other)
    {
        if(other.base_)
        {
            base_ = reinterpret_cast<base*>(&buf_[0]);
            other.base_->move(buf_);
            other.base_ = nullptr;
        }
    }

    invokable&
    operator=(invokable&& other)
    {
        // Engaged invokables must be invoked before
        // assignment otherwise the io_service
        // invariants are broken w.r.t completions.
        assert(! base_);

        if(other.base_)
        {
            base_ = reinterpret_cast<base*>(&buf_[0]);
            other.base_->move(buf_);
            other.base_ = nullptr;
        }
        return *this;
    }

    template<class F>
    void
    emplace(F&& f);

    void
    maybe_invoke()
    {
        if(base_)
        {
            auto const basep = base_;
            base_ = nullptr;
            (*basep)();
        }
    }
};

template<class F>
void
invokable::emplace(F&& f)
{
    static_assert(sizeof(buf_type) >= sizeof(holder<F>),
        "buffer too small");
    assert(! base_);
    ::new(buf_) holder<F>(std::forward<F>(f));
    base_ = reinterpret_cast<base*>(&buf_[0]);
}

} // detail
} // websocket
} // beast

#endif
