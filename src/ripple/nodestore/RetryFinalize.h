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

#ifndef RIPPLE_NODESTORE_RETRYFINALIZE_H_INCLUDED
#define RIPPLE_NODESTORE_RETRYFINALIZE_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <functional>

namespace ripple {
namespace NodeStore {

class RetryFinalize
{
public:
    using retryFunction = std::function<void(std::uint32_t shardIndex)>;

    RetryFinalize() = default;

    bool
    retry(Application& app, retryFunction f, std::uint32_t shardIndex);

    // Must match the imported shard's last ledger hash
    uint256 referenceHash{0};

private:
    using waitable_timer =
        boost::asio::basic_waitable_timer<std::chrono::steady_clock>;

    static constexpr std::chrono::seconds retryInterval_ =
        std::chrono::seconds{60};

    // Maximum attempts to retrieve a shard's last ledger hash
    static constexpr uint32_t maxAttempts_{5};

    std::unique_ptr<waitable_timer> timer_;

    // Number of attempts to retrieve a shard's last ledger hash
    std::uint32_t numAttempts_{0};
};

}  // namespace NodeStore
}  // namespace ripple

#endif  // RIPPLE_NODESTORE_RETRYFINALIZE_H_INCLUDED
