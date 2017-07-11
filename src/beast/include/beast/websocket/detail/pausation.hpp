//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_PAUSATION_HPP
#define BEAST_WEBSOCKET_DETAIL_PAUSATION_HPP

#include <beast/core/handler_ptr.hpp>
#include <boost/assert.hpp>
#include <array>
#include <memory>
#include <new>
#include <utility>

namespace beast {
namespace websocket {
namespace detail {

// A container that holds a suspended, asynchronous composed
// operation. The contained object may be invoked later to
// resume the operation, or the container may be destroyed.
//
class pausation
{
    struct base
    {
        base() = default;
        base(base &&) = default;
        virtual ~base() = default;
        virtual base* move(void* p) = 0;
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

        base*
        move(void* p) override
        {
            return ::new(p) holder(std::move(*this));
        }

        void
        operator()() override
        {
            F f_(std::move(f));
            this->~holder();
            // invocation of f_() can
            // assign a new object to *this.
            f_();
        }
    };

    struct exemplar
    {
        struct H
        {
            void operator()();
        };

        struct T
        {
            using handler_type = H;
        };

        handler_ptr<T, H> hp;

        void operator()();
    };

    using buf_type = char[sizeof(holder<exemplar>)];

    base* base_ = nullptr;
    alignas(holder<exemplar>) buf_type buf_;

public:
    ~pausation()
    {
        if(base_)
            base_->~base();
    }

    pausation() = default;

    pausation(pausation&& other)
    {
        if(other.base_)
        {
            base_ = other.base_->move(buf_);
            other.base_ = nullptr;
        }
    }

    pausation&
    operator=(pausation&& other)
    {
        // Engaged pausations must be invoked before
        // assignment otherwise the io_service
        // completion invariants are broken.
        BOOST_ASSERT(! base_);

        if(other.base_)
        {
            base_ = other.base_->move(buf_);
            other.base_ = nullptr;
        }
        return *this;
    }

    template<class F>
    void
    emplace(F&& f);

    bool
    maybe_invoke()
    {
        if(base_)
        {
            auto const basep = base_;
            base_ = nullptr;
            (*basep)();
            return true;
        }
        return false;
    }
};

template<class F>
void
pausation::emplace(F&& f)
{
    using type = holder<typename std::decay<F>::type>;
    static_assert(sizeof(buf_type) >= sizeof(type),
        "buffer too small");
    BOOST_ASSERT(! base_);
    base_ = ::new(buf_) type{std::forward<F>(f)};
}

} // detail
} // websocket
} // beast

#endif
