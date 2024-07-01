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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/rpc/ShardVerificationScheduler.h>

namespace ripple {
namespace RPC {

ShardVerificationScheduler::ShardVerificationScheduler(
    std::chrono::seconds retryInterval,
    std::uint32_t maxAttempts)
    : retryInterval_(
          (retryInterval == std::chrono::seconds(0) ? defaultRetryInterval_
                                                    : retryInterval))
    , maxAttempts_(maxAttempts == 0 ? defaultmaxAttempts_ : maxAttempts)
{
}

bool
ShardVerificationScheduler::retry(
    Application& app,
    bool shouldHaveHash,
    retryFunction f)
{
    if (numAttempts_ >= maxAttempts_)
        return false;

    // Retry attempts only count when we
    // have a validated ledger with a
    // sequence later than the shard's
    // last ledger.
    if (shouldHaveHash)
        ++numAttempts_;

    if (!timer_)
        timer_ = std::make_unique<waitable_timer>(app.getIOService());

    timer_->expires_from_now(retryInterval_);
    timer_->async_wait(f);

    return true;
}

void
ShardVerificationScheduler::reset()
{
    numAttempts_ = 0;
}

}  // namespace RPC
}  // namespace ripple
