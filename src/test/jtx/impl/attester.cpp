//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <test/jtx/attester.h>

#include <ripple/basics/Slice.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/STXChainBridge.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/XChainAttestations.h>

namespace ripple {
namespace test {
namespace jtx {

Buffer
sign_claim_attestation(
    PublicKey const& pk,
    SecretKey const& sk,
    STXChainBridge const& bridge,
    AccountID const& sendingAccount,
    STAmount const& sendingAmount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t claimID,
    std::optional<AccountID> const& dst)
{
    auto const toSign = Attestations::AttestationClaim::message(
        bridge,
        sendingAccount,
        sendingAmount,
        rewardAccount,
        wasLockingChainSend,
        claimID,
        dst);
    return sign(pk, sk, makeSlice(toSign));
}

Buffer
sign_create_account_attestation(
    PublicKey const& pk,
    SecretKey const& sk,
    STXChainBridge const& bridge,
    AccountID const& sendingAccount,
    STAmount const& sendingAmount,
    STAmount const& rewardAmount,
    AccountID const& rewardAccount,
    bool wasLockingChainSend,
    std::uint64_t createCount,
    AccountID const& dst)
{
    auto const toSign = Attestations::AttestationCreateAccount::message(
        bridge,
        sendingAccount,
        sendingAmount,
        rewardAmount,
        rewardAccount,
        wasLockingChainSend,
        createCount,
        dst);
    return sign(pk, sk, makeSlice(toSign));
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
