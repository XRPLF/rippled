//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_HANDLER_PTR_HPP
#define BEAST_IMPL_HANDLER_PTR_HPP

#include <beast/core/handler_helpers.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/assert.hpp>
#include <memory>

namespace beast {

template<class T, class Handler>
template<class DeducedHandler, class... Args>
inline
handler_ptr<T, Handler>::P::
P(DeducedHandler&& h, Args&&... args)
    : n(1)
    , handler(std::forward<DeducedHandler>(h))
{
    t = reinterpret_cast<T*>(
        beast_asio_helpers::
            allocate(sizeof(T), handler));
    try
    {
        t = new(t) T{handler,
            std::forward<Args>(args)...};
    }
    catch(...)
    {
        beast_asio_helpers::
            deallocate(t, sizeof(T), handler);
        throw;
    }
}

template<class T, class Handler>
handler_ptr<T, Handler>::
~handler_ptr()
{
    if(! p_)
        return;
    if(--p_->n)
        return;
    if(p_->t)
    {
        p_->t->~T();
        beast_asio_helpers::
            deallocate(p_->t, sizeof(T), p_->handler);
    }
    delete p_;
}

template<class T, class Handler>
handler_ptr<T, Handler>::
handler_ptr(handler_ptr&& other)
    : p_(other.p_)
{
    other.p_ = nullptr;
}

template<class T, class Handler>
handler_ptr<T, Handler>::
handler_ptr(handler_ptr const& other)
    : p_(other.p_)
{
    if(p_)
        ++p_->n;
}

template<class T, class Handler>
template<class... Args>
handler_ptr<T, Handler>::
handler_ptr(Handler&& handler, Args&&... args)
    : p_(new P{std::move(handler),
        std::forward<Args>(args)...})
{
    static_assert(! std::is_array<T>::value,
        "T must not be an array type");
}

template<class T, class Handler>
template<class... Args>
handler_ptr<T, Handler>::
handler_ptr(Handler const& handler, Args&&... args)
    : p_(new P{handler, std::forward<Args>(args)...})
{
    static_assert(! std::is_array<T>::value,
        "T must not be an array type");
}

template<class T, class Handler>
auto
handler_ptr<T, Handler>::
release_handler() ->
    handler_type
{
    BOOST_ASSERT(p_);
    BOOST_ASSERT(p_->t);
    p_->t->~T();
    beast_asio_helpers::
        deallocate(p_->t, sizeof(T), p_->handler);
    p_->t = nullptr;
    return std::move(p_->handler);
}

template<class T, class Handler>
template<class... Args>
void
handler_ptr<T, Handler>::
invoke(Args&&... args)
{
    BOOST_ASSERT(p_);
    BOOST_ASSERT(p_->t);
    p_->t->~T();
    beast_asio_helpers::
        deallocate(p_->t, sizeof(T), p_->handler);
    p_->t = nullptr;
    p_->handler(std::forward<Args>(args)...);
}

} // beast

#endif
