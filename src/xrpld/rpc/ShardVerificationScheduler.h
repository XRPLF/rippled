//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_SHARDVERIFICATIONSCHEDULER_H_INCLUDED
#define RIPPLE_RPC_SHARDVERIFICATIONSCHEDULER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <functional>

namespace ripple {
namespace RPC {

class ShardVerificationScheduler
{
public:
    // This is the signature of the function that the client
    // wants to have invoked upon timer expiration. The function
    // should check the error code 'ec' and abort the function
    // if the timer was cancelled:
    // (ec == boost::asio::error::operation_aborted).
    // In the body of the function, the client should perform
    // the necessary verification.
    using retryFunction =
        std::function<void(boost::system::error_code const& ec)>;

    ShardVerificationScheduler() = default;

    ShardVerificationScheduler(
        std::chrono::seconds retryInterval,
        std::uint32_t maxAttempts);

    bool
    retry(Application& app, bool shouldHaveHash, retryFunction f);

    void
    reset();

private:
    using waitable_timer =
        boost::asio::basic_waitable_timer<std::chrono::steady_clock>;

    /////////////////////////////////////////////////////
    // NOTE: retryInterval_ and maxAttempts_ were chosen
    // semi-arbitrarily and experimenting with other
    // values might prove useful.
    /////////////////////////////////////////////////////

    static constexpr std::chrono::seconds defaultRetryInterval_{60};

    static constexpr std::uint32_t defaultmaxAttempts_{5};

    // The number of seconds to wait before retrying
    // retrieval of a shard's last ledger hash
    const std::chrono::seconds retryInterval_{defaultRetryInterval_};

    // Maximum attempts to retrieve a shard's last ledger hash
    const std::uint32_t maxAttempts_{defaultmaxAttempts_};

    std::unique_ptr<waitable_timer> timer_;

    // Number of attempts to retrieve a shard's last ledger hash
    std::uint32_t numAttempts_{0};
};

}  // namespace RPC
}  // namespace ripple

#endif  // RIPPLE_RPC_SHARDVERIFICATIONSCHEDULER_H_INCLUDED
