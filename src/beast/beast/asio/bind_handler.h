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

#ifndef BEAST_ASIO_BIND_HANDLER_H_INCLUDED
#define BEAST_ASIO_BIND_HANDLER_H_INCLUDED

#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_cont_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>
#include <functional>
#include <type_traits>
#include <utility>

namespace beast {

namespace detail {

/** Nullary handler that calls Handler with bound arguments.

    The bound handler provides the same io_service execution
    guarantees as the original handler.
*/
template<class Handler, class... Args>
class bound_handler
{
private:
    using args_type = std::tuple<std::decay_t <Args>...>;

    Handler h_;
    args_type args_;

    template<class Tuple, std::size_t... S>
    static void invoke(Handler& h, Tuple& args,
        std::index_sequence <S...>)
    {
        h(std::get<S>(args)...);
    }

public:
    using result_type = void;

    template<class DeducedHandler>
    explicit
    bound_handler(DeducedHandler&& handler, Args&&... args)
        : h_(std::forward<DeducedHandler>(handler))
        , args_(std::forward<Args>(args)...)
    {
    }

    void
    operator()()
    {
        invoke(h_, args_,
            std::index_sequence_for<Args...> ());
    }

    void
    operator()() const
    {
        invoke(h_, args_,
            std::index_sequence_for<Args...> ());
    }

    friend
    void*
    asio_handler_allocate(
        std::size_t size, bound_handler* h)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, h->h_);
    }

    friend
    void
    asio_handler_deallocate(
        void* p, std::size_t size, bound_handler* h)
    {
        boost_asio_handler_alloc_helpers::
            deallocate(p, size, h->h_);
    }

    friend
    bool
    asio_handler_is_continuation(bound_handler* h)
    {
        return boost_asio_handler_cont_helpers::
            is_continuation (h->h_);
    }

    template<class F>
    friend
    void
    asio_handler_invoke(F&& f, bound_handler* h)
    {
        boost_asio_handler_invoke_helpers::
            invoke(f, h->h_);
    }
};

} // detail

//------------------------------------------------------------------------------

/** Binds parameters to a handler to produce a nullary functor.

    The returned handler provides the same io_service execution guarantees
    as the original handler. This is designed to use as a replacement for
    io_service::wrap, to ensure that the handler will not be invoked
    immediately by the calling function.
*/
template<class Handler, class... Args>
auto
bind_handler(Handler&& handler, Args&&... args)
{
    return detail::bound_handler<
        std::decay_t<Handler>, Args...>(
            std::forward<Handler>(handler),
                std::forward<Args>(args)...);
}

} // beast

namespace std {
template<class Handler, class... Args>
void bind(beast::detail::bound_handler<
    Handler, Args...>, ...) = delete;
} // std

#endif
