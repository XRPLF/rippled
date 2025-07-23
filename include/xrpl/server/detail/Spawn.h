//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright(c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_SERVER_SPAWN_H_INCLUDED
#define RIPPLE_SERVER_SPAWN_H_INCLUDED

#include <boost/asio/spawn.hpp>
#include <boost/asio/strand.hpp>

#include <concepts>
#include <type_traits>

namespace ripple::util {
namespace impl {

template <typename T>
concept IsStrand = std::same_as<
    std::decay_t<T>,
    boost::asio::strand<typename std::decay_t<T>::inner_executor_type>>;

/**
 * @brief A completion handler that restores `boost::asio::spawn`'s behaviour
 * from Boost 1.83
 *
 * This is intended to be passed as the third argument to `boost::asio::spawn`
 * so that exceptions are not ignored but propagated to `io_context.run()` call
 * site.
 */
inline constexpr struct PropagatingCompletionHandler
{
    /**
     * @brief The completion handler
     * @param ePtr The exception that was caught on the coroutine
     */
    void
    operator()(std::exception_ptr ePtr)
    {
        if (ePtr)
            std::rethrow_exception(ePtr);
    }
} kPROPAGATE_EXCEPTIONS;

}  // namespace impl

/**
 * @brief Spawns a coroutine using `boost::asio::spawn`
 *
 * @note This uses kPROPAGATE_EXCEPTIONS to force asio to propagate exceptions
 * through `io_context`
 * @note Since implicit strand was removed from boost::asio::spawn this helper
 * function adds the strand back
 *
 * @tparam Ctx The type of the context/strand
 * @tparam F The type of the function to execute
 * @param ctx The execution context
 * @param func The function to execute. Must return `void`
 */
template <typename Ctx, typename F>
    requires std::is_invocable_r_v<void, F, boost::asio::yield_context>
void
spawn(Ctx&& ctx, F&& func)
{
    if constexpr (impl::IsStrand<Ctx>)
    {
        boost::asio::spawn(
            std::forward<Ctx>(ctx),
            std::forward<F>(func),
            impl::kPROPAGATE_EXCEPTIONS);
    }
    else
    {
        boost::asio::spawn(
            boost::asio::make_strand(std::forward<Ctx>(ctx).get_executor()),
            std::forward<F>(func),
            impl::kPROPAGATE_EXCEPTIONS);
    }
}

}  // namespace ripple::util

#endif
