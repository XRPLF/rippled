//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_IMPL_HANDLER_PTR_HPP
#define BEAST_IMPL_HANDLER_PTR_HPP

#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
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
    using boost::asio::asio_handler_allocate;
    t = reinterpret_cast<T*>(
        asio_handler_allocate(
            sizeof(T), std::addressof(handler)));
    try
    {
        t = new(t) T{handler,
            std::forward<Args>(args)...};
    }
    catch(...)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            t, sizeof(T), std::addressof(handler));
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
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p_->t, sizeof(T), std::addressof(p_->handler));
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
    BOOST_STATIC_ASSERT(! std::is_array<T>::value);
}

template<class T, class Handler>
template<class... Args>
handler_ptr<T, Handler>::
handler_ptr(Handler const& handler, Args&&... args)
    : p_(new P{handler, std::forward<Args>(args)...})
{
    BOOST_STATIC_ASSERT(! std::is_array<T>::value);
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
    using boost::asio::asio_handler_deallocate;
    asio_handler_deallocate(
        p_->t, sizeof(T), std::addressof(p_->handler));
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
    using boost::asio::asio_handler_deallocate;
    asio_handler_deallocate(
        p_->t, sizeof(T), std::addressof(p_->handler));
    p_->t = nullptr;
    p_->handler(std::forward<Args>(args)...);
}

} // beast

#endif
