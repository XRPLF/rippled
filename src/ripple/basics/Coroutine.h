//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_COROUTINE_H_INCLUDED
#define RIPPLE_BASICS_COROUTINE_H_INCLUDED

#include <ripple/basics/promises.h>

#include <cassert>
#include <memory>

namespace ripple {

template <typename R>
class Coroutine
{
private:
    FuturePtr<R> output_;

protected:
    Scheduler& jscheduler_;

    explicit Coroutine(Scheduler& scheduler)
        : output_(scheduler.pending<R>()), jscheduler_(scheduler)
    {
    }

    Coroutine(Coroutine const&) = delete;
    Coroutine&
    operator=(Coroutine const&) = delete;
    virtual ~Coroutine() = default;

    virtual void
    start_() = 0;

    // TODO: Monomorphic base type for futures.
    template <typename U>
    bool
    threw(FuturePtr<U> const& input)
    {
        if (input->state() == REJECTED)
        {
            output_->reject(input->error());
            return true;
        }
        assert(input->state() == FULFILLED);
        return false;
    }

    void
    throw_(std::string const& reason)
    {
        [[maybe_unused]] auto rejected =
            output_->reject(std::runtime_error(reason));
        assert(rejected);
    }

    void
    return_(R&& output)
    {
        [[maybe_unused]] auto fulfilled = output_->fulfill(std::move(output));
        assert(fulfilled);
    }

    void
    return_(FuturePtr<R>&& output)
    {
        [[maybe_unused]] auto linked = output_->link(std::move(output));
        assert(linked);
    }

public:
    FuturePtr<R>
    start()
    {
        auto output = output_;
        start_();
        return output;
    }
};

template <typename Coroutine, typename... Args>
static auto
start(Args&&... args)
{
    auto coroutine = std::make_unique<Coroutine>(std::forward<Args>(args)...);
    auto future = coroutine->start();
    future->subscribe([c = coroutine.get()](auto const&) { delete c; });
    coroutine.release();
    return future;
}

}  // namespace ripple

#endif
